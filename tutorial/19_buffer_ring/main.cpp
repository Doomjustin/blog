#include <cstdlib>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

auto session(net::ip::tcp::socket client, int chat_bgid) -> async::Task<>
{
    // 将 ReceiveStream 绑定到定制的内存池上
    auto stream = client.receive_stream(chat_bgid);

    log::info("[Server] 开始接收弹幕流...");
    int msg_count = 0;

    while (auto msg = co_await stream.next()) {
        auto data_view = as_string(msg->data());
        log::info("[Server] 收到弹幕 ({} bytes): {}", data_view.size(), data_view);
        
        if (++msg_count == 3) break; // 收到 3 条后退出演示
    }
}

auto server(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    // 💥 核心：针对小包（如聊天/弹幕）定制内存池
    // entries=8192 (槽位极多，防突发), size=256 (单包极小，完美适配 CPU Cache)
    auto chat_bgid = async::this_coroutine::setup_buffer_ring(8192, 256);
    log::info("[Server] 已分配定制化弹幕 Buffer Ring (BGID: {})", chat_bgid);

    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };
    auto client = co_await acceptor.async_accept();
    if (!client) co_return;

    co_await session(std::move(*client), chat_bgid);
}

auto client(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    co_await async::sleep_for(50ms); 

    net::ip::tcp::socket sock{ endpoint.protocol() };
    sock.connect(endpoint);

    // 疯狂连发 3 条小包弹幕
    co_await net::send(sock, async::buffer("666666!"));
    co_await async::sleep_for(10ms);
    co_await net::send(sock, async::buffer("前方高能预警"));
    co_await async::sleep_for(10ms);
    co_await net::send(sock, async::buffer("完结撒花~~"));
}

auto run_demo() -> async::Task<>
{
    log::info("=== Demo: Buffer Ring Tuning ===");
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 10088 };

    co_await async::all(
        server(endpoint),
        client(endpoint)
    );
}

} // namespace

int main()
{
    async::run(run_demo);
    return EXIT_SUCCESS;
}