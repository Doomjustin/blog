#include <cstdlib>
#include <vector>

#include <blog.h>

using namespace std::chrono_literals;

// 静态模拟一个大文件 (10MB)
constexpr std::array<std::byte, 10 * 1024 * 1024> HUGE_FILE{};

auto server(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };
    auto client = co_await acceptor.async_accept();
    if (!client) co_return;

    log::info("[Server] 开始发送 10MB 巨型文件...");

    // 💥 核心：编译期契约，打上 ZeroCopyT 标签
    auto zc_buffer = net::zero_copy(HUGE_FILE);

    // 底层会匹配到 SendAllZCAwaiter，触发 IORING_OP_SEND_ZC
    auto result = co_await net::send(*client, zc_buffer);

    if (result)
        log::info("[Server] 10MB 发送完毕。内核已释放对内存的物理锁定 (Notif CQE 已到达)。");
}

auto client(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    co_await async::sleep_for(50ms); 

    net::ip::tcp::socket sock{ endpoint.protocol() };
    sock.connect(endpoint);

    std::vector<std::byte> buf{ 1024 * 1024 }; // 每次收 1MB
    std::size_t total_received = 0;

    while (true) {
        auto res = co_await sock.async_receive_some(async::buffer(buf));
        if (!res || *res == 0) break;
        total_received += *res;
    }

    log::info("[Client] 接收完毕，共收到 {} 字节。", total_received);
}

auto run_demo() -> async::Task<>
{
    log::info("=== Demo: Zero-copy Send ===");
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 10087 };

    co_await async::all(server(endpoint), client(endpoint));
}

int main()
{
    async::run(run_demo);
    return EXIT_SUCCESS;
}