#include <cstdlib>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

constexpr int CLIENT_COUNT = 100;
constexpr int PING_PONG_PER_CLIENT = 10000;
constexpr int TOTAL_REQUESTS = CLIENT_COUNT * PING_PONG_PER_CLIENT;

auto tpc_session(net::ip::tcp::socket sock, unsigned bgid) -> async::Task<>
{
    // 关闭 Nagle 算法，消除 40ms 延迟
    sock.option(net::ip::tcp::socket::no_delay{true});

    auto stream = sock.receive_stream(bgid);
    while (auto msg = co_await stream.next())
        co_await net::send(sock, msg->data());
}

auto tpc_server(net::ip::tcp::endpoint ep) -> async::Task<>
{
    auto bgid = async::this_coroutine::setup_buffer_ring(8192, 1024);

    auto acceptor = net::ip::tcp::acceptor{ ep, true };
    while (true) {
        auto client = co_await acceptor.async_accept();
        
        if (client)
            async::co_spawn(tpc_session(std::move(*client), bgid));
    }
}

} // namespace

int main()
{
    log::info("=== TPC 框架 Echo Server (io_uring + Buffer Ring) ===");

    auto ep = net::ip::tcp::endpoint{net::ip::AddressV4::loopback(), 10087};

    log::info("TPC echo server 监听端口 10087，等待客户端连接...");
    async::run(tpc_server, ep);

    return EXIT_SUCCESS;
}
