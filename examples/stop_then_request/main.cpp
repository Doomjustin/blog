#include <array>
#include <chrono>
#include <cstdlib>
#include <stop_token>
#include <thread>

#include <blog.h>

namespace {

auto session(net::ip::tcp::socket client) -> async::Task<>
{
    std::array<std::byte, 4096> buf{};
    while (true) {
        auto recv = co_await client.async_receive_some(buf);
        if (!recv || *recv == 0)
            co_return;

        co_await async::sleep_for(std::chrono::milliseconds{ 700 });

        if (auto sent = co_await net::send(client, std::span{ buf.data(), *recv }); !sent)
            co_return;
    }
}

auto echo_server(std::uint16_t& out_port) -> async::Task<>
{
    net::ip::tcp::endpoint endpoint{ net::ip::AddressV4::loopback(), 0 };
    net::ip::tcp::acceptor acceptor{ endpoint };

    if (auto ep = local_endpoint(acceptor))
        out_port = ep->port();   // signal the client that the server is ready

    async::Scope sessions;

    while (true) {
        auto client = co_await acceptor.async_accept();
        if (!client)
            break;
        sessions.spawn(session(std::move(*client)));
    }

    co_await sessions.join();
}

auto one_request(net::ip::tcp::socket& sock,
                 std::string_view message,
                 std::stop_token stop)
    -> async::Task<std::expected<std::string, std::error_code>>
{
    auto payload = async::buffer(message);

    // Per-op cancellation: stop is observed at the next I/O boundary.
    auto sent = co_await async::stop_then(sock.async_send_some(payload), stop);
    if (!sent)
        co_return std::unexpected(sent.error());

    std::array<std::byte, 256> buf{};
    auto recv = co_await async::stop_then(sock.async_receive_some(buf), stop);
    if (!recv)
        co_return std::unexpected(recv.error());

    co_return std::string{ reinterpret_cast<const char*>(buf.data()), *recv };
}

auto client(std::stop_token stop, const std::uint16_t& server_port) -> async::Task<>
{
    while (server_port == 0)
        co_await async::sleep_for(std::chrono::milliseconds{ 1 });

    net::ip::tcp::endpoint endpoint{ net::ip::AddressV4::loopback(), server_port };
    net::ip::tcp::socket sock{ net::ip::tcp::v4() };
    sock.connect(endpoint);
    log::info("[client] connected to :{}", server_port);

    for (int i = 0; ; ++i) {
        auto msg = "request-" + std::to_string(i);
        auto result = co_await one_request(sock, msg, stop);

        if (!result) {
            const auto& ec = result.error();
            if (ec == std::errc::operation_canceled)
                log::info("[client] request #{} cancelled — exiting cleanly", i);
            else
                log::error("[client] request #{} failed: {}", i, ec);
            co_return;
        }

        log::info("[client] echo: {}", *result);

        // Keep the loop tight so there is almost always an in-flight receive.
        co_await async::sleep_for(std::chrono::milliseconds{ 50 });
    }
}

auto demo() -> async::Task<>
{
    std::stop_source shutdown;

    std::jthread timer{ [source = shutdown]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1500 });
        log::info("[timer] requesting stop");
        source.request_stop();
    } };

    std::uint16_t server_port = 0;

    async::co_spawn(echo_server(server_port));
    co_await client(shutdown.get_token(), server_port);

    log::info("[demo] completed cleanly");
    async::stop();
}

} // namespace

int main()
{
    async::run(demo);
    return EXIT_SUCCESS;
}
