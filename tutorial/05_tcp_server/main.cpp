#include <array>

#include <blog.h>

namespace {

// 处理单个连接：收到多少字节就回显多少，直到对端关闭
auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer)
    -> async::Task<>
{
    log::info("connected: {}", peer);

    std::array<std::byte, 4096> buf;
    while (true) {
        auto recv_result = co_await client.async_receive_some(buf);
        if (!recv_result) {
            log::error("recv error from {}: {}", peer, recv_result.error());
            co_return;
        }

        if (*recv_result == 0) {         // EOF：对端正常关闭
            log::info("disconnected: {}", peer);
            co_return;
        }

        auto send_result = co_await net::send(client, std::span{ buf.data(), *recv_result });
        if (!send_result) {
            log::error("send error to {}: {}", peer, send_result.error());
            co_return;
        }
    }
}

auto server() -> async::Task<>
{
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), 12345 };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, /*reuse_port=*/true };
    log::info("listening on {}", endpoint);

    while (true) {
        net::ip::tcp::endpoint peer;
        auto accept_result = co_await acceptor.async_accept(peer);
        if (!accept_result) {
            if (accept_result.error() == std::errc::operation_canceled)
                co_return;
            
            log::error("accept error: {}", accept_result.error());
            continue;
        }
        // 每个连接派给独立协程，acceptor 立即继续等待下一个连接
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
