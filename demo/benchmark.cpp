#include <cstdlib>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "buffer.h"
#include "co_spawn.h"
#include "io_context.h"
#include "ip/address.h"
#include "ip/tcp.h"
#include "signals.h"
#include "task.h"
#include "write.h"

static const auto header = std::string{ "HTTP/1.1 200 OK\r\nContent-Length: 65536\r\n\r\n" };

const auto response_ok = [] {
    constexpr std::size_t body_size = 64 * 1024;
    static const auto body = std::string(body_size, 'x');
    return header + body;
}();

const auto response_sequence = [] {
    std::vector<std::string_view> result;

    static const auto body = std::string(64 * 1024, 'x');
    result.push_back(header);
    result.push_back(body);

    return result;
} ();

class Cluster {
public:
    explicit Cluster(std::size_t thread_count)
      : contexts_{ thread_count }
    {
        // 监听系统信号的协程只需要在一个io_context上注册就行了，其他的io_context不需要注册
        co_spawn(contexts_[0], shutdown_monitor());
    }

    void run()
    {
        for (std::size_t i = 1; i < contexts_.size(); ++i)
            threads_.emplace_back([&context = contexts_[i]] { context.run(); });

        contexts_[0].run();
    }

    // 这里的submit方法是为了简化示例代码，实际使用中可能需要更灵活的调度策略
    template<typename Awaiter>
    void submit(Awaiter&& awaitable) noexcept
    {
        for (auto& context: contexts_)
            co_spawn(context, std::invoke(std::forward<Awaiter>(awaitable), context));
    }

    auto context(std::size_t index) -> IOContext&
    {
        return contexts_[index];
    }

    auto operator[](std::size_t index) -> IOContext&
    {
        return contexts_[index];
    }

private:
    std::vector<std::jthread> threads_;
    std::deque<IOContext> contexts_;
        
    auto shutdown_monitor() -> Task<>
    {
        using namespace std::chrono_literals;
        
        // 把signal set注册到第一个io_context上就行了
        SignalSet sets{ contexts_[0], signals::interrupt, signals::terminate };
        
        co_await sets.async_wait();
        spdlog::info("Received shutdown signal, stopping IOContext...");

        // 只停止io_context的run循环，等待所有线程退出
        for (auto& context: contexts_)
            context.stop();
    }
};


constexpr auto is_peer_shutdown(std::error_code& ec) -> bool
{
    return ec == std::errc::connection_reset || 
           ec == std::errc::broken_pipe ||
           ec == std::errc::connection_aborted ||
           ec == std::errc::operation_canceled;
}


template<typename Context>
auto session(ip::tcp::socket<Context> socket) -> Task<>
{
    std::array<char, 1024> data{};
    while (true) {
        auto bytes_read = co_await socket.async_read_some(buffer(data));
        if (!bytes_read) {
            if (!is_peer_shutdown(bytes_read.error()))
                spdlog::error("Failed to read from client: {}", bytes_read.error().message());

            break;
        }

        auto bytes_written = co_await write(socket, buffer(response_ok));
        // auto bytes_written = co_await socket.async_write_some(response_sequence);
        if (!bytes_written) {
            if (!is_peer_shutdown(bytes_written.error()))
                spdlog::error("Failed to write to client: {}", bytes_written.error().message());

            break;
        }
    }
}

auto http_server(IOContext& context) -> Task<>
{
    auto server_endpoint = ip::tcp::endpoint{ ip::AddressV6::any(), 12345 };
    auto acceptor = ip::tcp::acceptor(context, server_endpoint, true);

    ip::tcp::endpoint client_endpoint;
    while (true) {
        auto client = co_await acceptor.async_accept(client_endpoint);
        if (!client) {
            spdlog::error("Failed to accept client connection: {}", client.error().message());
            continue;
        }

        co_spawn(context, session(std::move(*client)));
    }
}


int main(int argc, char* argv[])
{
    ::signal(SIGPIPE, SIG_IGN);
    
    auto worker_count = std::thread::hardware_concurrency() == 0 ? 4 : std::thread::hardware_concurrency();
    Cluster cluster{ worker_count };
    cluster.submit(http_server);
    
    cluster.run();

    return EXIT_SUCCESS;
}