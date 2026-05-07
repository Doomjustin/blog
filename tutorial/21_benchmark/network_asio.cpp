#include <cstdlib>

#include <asio.hpp>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

constexpr int CLIENT_COUNT = 100;
constexpr int PING_PONG_PER_CLIENT = 10000;
constexpr int TOTAL_REQUESTS = CLIENT_COUNT * PING_PONG_PER_CLIENT;

auto asio_session(asio::ip::tcp::socket sock) -> asio::awaitable<void>
{
    // 关闭 Nagle 算法，消除 40ms 延迟
    sock.set_option(asio::ip::tcp::no_delay(true));

    char buf[1024];
    try {
        while (true) {
            std::size_t n = co_await sock.async_read_some(
                asio::buffer(buf), asio::use_awaitable);
            co_await asio::async_write(sock, asio::buffer(buf, n),
                                       asio::use_awaitable);
        }
    } catch (const std::exception&) {}
}

auto asio_server(asio::ip::tcp::acceptor& acceptor) -> asio::awaitable<void>
{
    try {
        while (true) {
            auto sock = co_await acceptor.async_accept(asio::use_awaitable);
            asio::co_spawn(acceptor.get_executor(), asio_session(std::move(sock)), asio::detached);
        }
    } catch (...) {}
}

} // namespace

int main()
{
    log::info("=== Asio Echo Server ===");

    asio::io_context ctx(1);
    asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), 10086);
    asio::ip::tcp::acceptor acceptor(ctx, ep);

    asio::co_spawn(ctx, asio_server(acceptor), asio::detached);

    log::info("Asio echo server 监听端口 10086，等待客户端连接...");
    ctx.run();
    return EXIT_SUCCESS;
}
