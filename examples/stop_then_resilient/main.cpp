#include <array>
#include <chrono>
#include <cstdlib>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto classify_error(const std::error_code& ec) -> std::string_view
{
    if (ec == std::errc::operation_canceled)
        return "operation_canceled";
    if (ec == std::errc::timed_out)
        return "timed_out";
    if (ec == std::errc::connection_reset)
        return "connection_reset";
    return "other_error";
}

auto session(net::ip::tcp::socket client) -> async::Task<>
{
    std::array<std::byte, 256> buf{};
    auto recv = co_await client.async_receive_some(buf);
    if (!recv || *recv == 0)
        co_return;

    std::string req{ reinterpret_cast<const char*>(buf.data()), *recv };

    if (req.contains("slow"))
        co_await async::sleep_for(700ms);
    else
        co_await async::sleep_for(80ms);

    auto response = "ack:" + req;
    auto payload = async::buffer(response);
    (void)co_await net::send(client, payload);
}

auto server(std::uint16_t& out_port, std::stop_token stop) -> async::Task<>
{
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 0 };
    auto acceptor = net::ip::tcp::acceptor{ endpoint };

    if (auto ep = local_endpoint(acceptor))
        out_port = ep->port();

    while (true) {
        auto client = co_await async::stop_then(acceptor.async_accept(), stop);
        if (!client) {
            if (client.error() == std::errc::operation_canceled)
                co_return;
            log::error("[server] accept failed: {}", client.error());
            continue;
        }
        async::co_spawn(session(std::move(*client)));
    }
}

auto send_with_policy(net::ip::tcp::socket& sock,
                      std::string_view text,
                      std::stop_token stop)
    -> async::Task<std::expected<std::size_t, std::error_code>>
{
    auto bytes = async::buffer(text);
    std::error_code last = std::make_error_code(std::errc::io_error);

    for (int attempt = 1; attempt <= 3; ++attempt) {
        auto sent = co_await async::stop_then(sock.async_send_some(bytes), stop);
        if (sent)
            co_return sent;

        last = sent.error();
        if (last == std::errc::operation_canceled)
            co_return std::unexpected(last);

        if (attempt < 3)
            co_await async::sleep_for(std::chrono::milliseconds{ 50 * attempt });
    }

    co_return std::unexpected(last);
}

auto recv_with_policy(net::ip::tcp::socket& sock,
                      std::span<std::byte> buffer,
                      std::stop_token stop)
    -> async::Task<std::expected<std::size_t, std::error_code>>
{
    std::error_code last = std::make_error_code(std::errc::timed_out);

    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (stop.stop_requested())
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));

        auto recv = co_await async::timeout(sock.async_receive_some(buffer), 250ms);
        if (recv)
            co_return recv;

        last = recv.error();
        if (last != std::errc::timed_out)
            co_return std::unexpected(last);

        if (attempt < 3)
            co_await async::sleep_for(std::chrono::milliseconds{ 50 * attempt });
    }

    co_return std::unexpected(last);
}

auto one_rpc(const net::ip::tcp::endpoint& endpoint,
             std::string_view request,
             std::stop_token stop)
    -> async::Task<std::expected<std::string, std::error_code>>
{
    net::ip::tcp::socket sock{ net::ip::tcp::v4() };
    sock.connect(endpoint);

    auto sent = co_await send_with_policy(sock, request, stop);
    if (!sent)
        co_return std::unexpected(sent.error());

    std::array<std::byte, 256> buf{};
    auto recv = co_await recv_with_policy(sock, buf, stop);
    if (!recv)
        co_return std::unexpected(recv.error());

    co_return std::string{ reinterpret_cast<const char*>(buf.data()), *recv };
}

auto client(std::uint16_t& server_port, std::stop_token stop) -> async::Task<>
{
    while (server_port == 0)
        co_await async::sleep_for(1ms);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), server_port };

    std::vector<std::string> jobs{
        "fast-a", "slow-b", "fast-c", "slow-d", "fast-e", "fast-f"
    };

    for (const auto& job : jobs) {
        auto res = co_await one_rpc(endpoint, job, stop);
        if (!res) {
            auto ec = res.error();
            log::error("[client] job={} failed: {} ({})", job, classify_error(ec), ec);
            if (ec == std::errc::operation_canceled)
                co_return;
            continue;
        }

        log::info("[client] job={} ok -> {}", job, *res);
    }

    log::info("[client] all jobs processed");
}

auto demo() -> async::Task<>
{
    std::stop_source shutdown;
    std::uint16_t server_port = 0;

    std::jthread cancel_timer{ [source = shutdown]() mutable {
        std::this_thread::sleep_for(900ms);
        log::info("[timer] request_stop");
        source.request_stop();
    } };

    async::co_spawn(server(server_port, shutdown.get_token()));
    co_await client(server_port, shutdown.get_token());

    log::info("[demo] finished");
}

} // namespace

int main()
{
    async::run(demo);
    return EXIT_SUCCESS;
}
