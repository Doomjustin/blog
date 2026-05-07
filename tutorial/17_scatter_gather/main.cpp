#include <array>
#include <cstdlib>
#include <string>

#include <blog.h>

using namespace std::chrono_literals;

auto server(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };
    log::info("[Server] 监听中，等待客户端连接...");
    
    auto client = co_await acceptor.async_accept();
    if (!client) co_return;

    log::info("[Server] 客户端已连接，准备使用 Scatter/Gather 发送数据...");

    // 动态生成的 HTTP Header
    std::string header = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n";
    // 静态的 Body 数据
    std::string body = "Hello, World!";

    // 💥 核心：构建多段内存视图序列 (Sequence Buffer)
    // 底层会直接将其映射为 iovec 数组，并使用 writev 逻辑
    std::array<std::string_view, 2> buffers = { header, body };

    auto result = co_await client->async_send_some(buffers);

    if (result)
        log::info("[Server] 成功发送 {} 字节！零内存拼接，零 CPU 拷贝。", *result);
}

auto client(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    co_await async::sleep_for(50ms); // 等待服务端启动

    net::ip::tcp::socket sock{ endpoint.protocol() };
    sock.connect(endpoint);

    std::string buf(1024, '\0');
    auto res = co_await net::receive(sock, async::buffer(buf));
    
    if (res)
        log::info("[Client] 收到完整响应:\n{}", std::string_view{buf.data(), *res});
}

auto run_demo() -> async::Task<>
{
    log::info("=== Demo: Scatter/Gather I/O ===");
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 10086 };

    // 并发启动服务端与客户端
    co_await async::all(
        server(endpoint),
        client(endpoint)
    );
}

int main()
{
    async::run(run_demo);
    return EXIT_SUCCESS;
}