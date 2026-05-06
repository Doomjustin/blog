#include <blog.h>

namespace {

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    log::info("connected: {}", peer);

    auto stream = client.receive_stream();
    while (true) {
        auto result = co_await stream.next();
        if (!result) {
            if (result.error() != std::errc::operation_canceled)
                log::error("recv error from {}: {}", peer, result.error());
            co_return;
        }

        if (result->data().empty()) {   // EOF：对端正常关闭
            log::info("disconnected: {}", peer);
            co_return;
        }

        auto send_result = co_await net::send(client, result->data());
        if (!send_result) {
            log::error("send error to {}: {}", peer, send_result.error());
            co_return;
        }
    }
}

auto server() -> async::Task<>
{
    async::this_coroutine::setup_buffer_ring(128);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), 12345 };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, /*reuse_port=*/true };
    if (auto ep = local_endpoint(acceptor))
        log::info("listening on {}", *ep);

    while (true) {
        net::ip::tcp::endpoint peer;
        auto accept_result = co_await acceptor.async_accept(peer);
        if (!accept_result) {
            if (accept_result.error() == std::errc::operation_canceled)
                co_return;

            log::error("accept error: {}", accept_result.error());
            continue;
        }

        async::co_spawn(session(std::move(*accept_result), peer));
    }
}

auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    co_await signals.async_wait();
    log::info("shutting down...");
    async::stop();
}

auto run() -> async::Task<>
{
    async::co_spawn(shutdown_monitor());
    co_await server();
}

} // namespace

int main()
{
    async::run(run);
}
