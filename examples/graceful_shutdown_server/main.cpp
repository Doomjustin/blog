#include <array>
#include <cstdlib>
#include <stop_token>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto session(net::ip::tcp::socket client) -> async::Task<>
{
    std::array<std::byte, 64> buf{};
    auto recv = co_await client.async_receive_some(buf);
    if (!recv || *recv == 0)
        co_return;

    auto write = co_await net::send(client, std::span{ buf }.first(*recv));
    if (!write)
        log::error("[server] send failed: {}", write.error());
}

auto server(std::uint16_t& out_port, std::stop_token stop) -> async::Task<>
{
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 0 };
    auto acceptor = net::ip::tcp::acceptor{ endpoint };

    if (auto ep = local_endpoint(acceptor)) {
        out_port = ep->port();
        log::info("[server] listening on {}", *ep);
    }

    while (true) {
        auto client = co_await async::stop_then(acceptor.async_accept(), stop);
        if (!client) {
            if (client.error() == std::errc::operation_canceled) {
                log::info("[server] accept canceled, shutdown complete");
                co_return;
            }

            log::error("[server] accept failed: {}", client.error());
            continue;
        }

        async::co_spawn(session(std::move(*client)));
    }
}

auto probe_client(std::uint16_t& port) -> async::Task<>
{
    while (port == 0)
        co_await async::sleep_for(1ms);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), port };

    net::ip::tcp::socket sock{ net::ip::tcp::v4() };
    sock.connect(endpoint);

    auto payload = async::buffer("ping");
    auto sent = co_await net::send(sock, payload);
    if (!sent) {
        log::error("[client] send failed: {}", sent.error());
        co_return;
    }

    std::array<std::byte, 16> buf{};
    auto recv = co_await sock.async_receive_some(buf);
    if (!recv) {
        log::error("[client] recv failed: {}", recv.error());
        co_return;
    }

    auto text = std::string{ reinterpret_cast<const char*>(buf.data()), *recv };
    log::info("[client] got echo: {}", text);
}

auto run() -> async::Task<>
{
    std::stop_source stop;
    std::uint16_t port = 0;

    async::co_spawn(server(port, stop.get_token()));
    co_await probe_client(port);

    stop.request_stop();

    log::info("[demo] graceful shutdown finished");
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
