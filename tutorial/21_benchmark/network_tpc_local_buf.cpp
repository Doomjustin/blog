#include <array>
#include <cstdlib>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

constexpr int CLIENT_COUNT = 100;
constexpr int PING_PONG_PER_CLIENT = 10000;
constexpr int TOTAL_REQUESTS = CLIENT_COUNT * PING_PONG_PER_CLIENT;

auto tpc_session(net::ip::tcp::socket sock) -> async::Task<>
{
    sock.option(net::ip::tcp::socket::no_delay{true});

    std::array<std::byte, 1024> buf;
    while (true) {
        auto n = co_await sock.async_receive_some(buf);
        if (!n) co_return;
        co_await net::send(sock, std::span{ buf.data(), *n });
    }
}

auto tpc_server(net::ip::tcp::endpoint ep) -> async::Task<>
{
    auto acceptor = net::ip::tcp::acceptor{ ep, true };
    while (true) {
        auto client = co_await acceptor.async_accept();
        if (client)
            async::co_spawn(tpc_session(std::move(*client)));
    }
}

} // namespace

int main()
{
    log::info("=== TPC 框架 Echo Server (io_uring + 本地 buffer，无 Buffer Ring) ===");

    auto ep = net::ip::tcp::endpoint{net::ip::AddressV4::loopback(), 10088};

    log::info("TPC echo server 监听端口 10088，等待客户端连接...");
    async::run(tpc_server, ep);

    return EXIT_SUCCESS;
}
