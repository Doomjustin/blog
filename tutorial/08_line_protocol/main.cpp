#include <string>

#include <blog.h>

namespace {

// 从 pending 缓冲区中提取所有完整行（以 \n 结尾），逐行回显
// 返回已消费的字节数
auto flush_lines(std::string& pending, net::ip::tcp::socket& client,
                 const net::ip::tcp::endpoint& peer) -> async::Task<bool>
{
    std::string::size_type pos = 0;
    while (true) {
        auto nl = pending.find('\n', pos);
        if (nl == std::string::npos)
            break;

        // 去掉 \r（兼容 \r\n 行尾，例如 telnet）
        auto end = nl;
        if (end > pos && pending[end - 1] == '\r')
            --end;

        auto line = std::string_view{ pending }.substr(pos, end - pos);
        log::info("line: {}", line);

        auto reply = std::string{ line } + "\n";
        auto send_result = co_await net::send(client, async::buffer(reply));
        if (!send_result) {
            log::error("send error to {}: {}", peer, send_result.error());
            co_return false;
        }

        pos = nl + 1;
    }
    pending.erase(0, pos);
    co_return true;
}

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    log::info("connected: {}", peer);

    std::string pending;           // 跨 CQE 边界的不完整行
    auto stream = client.receive_stream();

    while (true) {
        auto result = co_await stream.next();
        if (!result) {
            if (result.error() != std::errc::operation_canceled)
                log::error("recv error from {}: {}", peer, result.error());
            co_return;
        }

        if (result->data().empty()) {
            // EOF：对端关闭了写端（half-close）
            // 缓冲区里可能还有最后一行没有 \n，也回显出去
            if (!pending.empty()) {
                log::info("line (no trailing newline): {}", pending);
                auto reply = pending + "\n";
                co_await net::send(client, async::buffer(reply));
            }
            log::info("disconnected: {}", peer);
            co_return;
        }

        pending += as_string(result->data());

        if (!co_await flush_lines(pending, client, peer))
            co_return;
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
