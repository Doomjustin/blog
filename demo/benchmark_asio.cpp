#include <array>
#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <sys/socket.h>

#include <asio.hpp>
#include <spdlog/spdlog.h>

namespace {

const auto response_ok = [] {
    constexpr std::size_t body_size = 64 * 1024;
    static const auto header = std::string{ "HTTP/1.1 200 OK\r\nContent-Length: 65536\r\n\r\n" };
    static const auto body = std::string(body_size, 'x');
    return header + body;
}();

constexpr std::string_view response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";

auto is_peer_shutdown(const asio::error_code& ec) -> bool
{
    return ec == asio::error::eof ||
           ec == asio::error::operation_aborted ||
           ec == asio::error::connection_reset ||
           ec == asio::error::connection_aborted ||
           ec == asio::error::broken_pipe;
}

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::ip::tcp;
using asio::use_awaitable;

class Cluster {
public:
    explicit Cluster(std::size_t thread_count)
      : contexts_(thread_count)
    {
        co_spawn(contexts_[0], shutdown_monitor(), detached);
    }

    void run()
    {
        for (std::size_t i = 1; i < contexts_.size(); ++i)
            threads_.emplace_back([&context = contexts_[i]] { context.run(); });

        contexts_[0].run();
    }

    template<typename Awaitable>
    void submit(Awaitable&& awaitable)
    {
        for (auto& context: contexts_)
            co_spawn(context, std::invoke(std::forward<Awaitable>(awaitable), context), detached);
    }

    auto context(std::size_t index) -> asio::io_context&
    {
        return contexts_[index];
    }

    auto operator[](std::size_t index) -> asio::io_context&
    {
        return contexts_[index];
    }

private:
    std::vector<std::jthread> threads_;
    std::deque<asio::io_context> contexts_;

    auto shutdown_monitor() -> awaitable<void>
    {
        asio::signal_set sets{ contexts_[0], SIGINT, SIGTERM };

        asio::error_code ec;
        co_await sets.async_wait(asio::redirect_error(use_awaitable, ec));
        if (ec)
            spdlog::error("asio signal error: {}", ec.message());
        else
            spdlog::info("Received shutdown signal, stopping IOContext...");

        for (auto& context: contexts_)
            context.stop();
    }
};

auto session(tcp::socket socket) -> awaitable<void>
{
    std::array<char, 1024> buffer{};

    asio::error_code ec;
    socket.set_option(tcp::no_delay(true), ec);
    if (ec) {
        spdlog::error("Failed to enable TCP_NODELAY: {}", ec.message());
        co_return;
    }

    while (true) {
        ec.clear();
        co_await socket.async_read_some(
            asio::buffer(buffer),
            asio::redirect_error(use_awaitable, ec));

        if (ec) {
            if (!is_peer_shutdown(ec))
                spdlog::error("Failed to read from client: {}", ec.message());

            co_return;
        }

        co_await asio::async_write(
            socket,
            asio::buffer(response),
            asio::redirect_error(use_awaitable, ec));

        if (ec) {
            if (!is_peer_shutdown(ec))
                spdlog::error("Failed to write to client: {}", ec.message());

            co_return;
        }
    }
}

auto http_server(asio::io_context& context) -> awaitable<void>
{
    auto reuse_port = asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT>{ true };

    auto endpoint = tcp::endpoint{ asio::ip::address_v6::any(), 12345 };
    auto acceptor = tcp::acceptor{ context };
    acceptor.open(endpoint.protocol());
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.set_option(reuse_port);
    acceptor.set_option(asio::ip::v6_only(false));
    acceptor.bind(endpoint);
    acceptor.listen(asio::socket_base::max_listen_connections);

    while (acceptor.is_open()) {
        asio::error_code ec;
        auto socket = co_await acceptor.async_accept(asio::redirect_error(use_awaitable, ec));
        if (ec) {
            if (ec == asio::error::operation_aborted)
                co_return;

            spdlog::error("Failed to accept client connection: {}", ec.message());
            continue;
        }

        co_spawn(context, session(std::move(socket)), detached);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    auto worker_count = std::thread::hardware_concurrency() == 0 ? 4 : std::thread::hardware_concurrency();
    Cluster cluster{ worker_count };
    cluster.submit(http_server);

    cluster.run();
    return EXIT_SUCCESS;
}
