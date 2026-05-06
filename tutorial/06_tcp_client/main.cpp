#include <blog.h>

namespace {

constexpr std::string_view MESSAGE = "hello, tutorial";

auto client() -> async::Task<>
{
    // 1. 构造目标地址
    auto endpoint = net::ip::tcp::endpoint::from_string("127.0.0.1", 12345);

    // 2. 创建 socket 并建立连接（同步；连接失败抛 std::system_error）
    auto socket = net::ip::tcp::socket{ endpoint.protocol() };
    try {
        socket.connect(endpoint);
    }
    catch (const std::exception& ex) {
        log::error("connect failed: {}", ex.what());
        co_return;
    }

    if (auto local = local_endpoint(socket), remote = remote_endpoint(socket); local && remote)
        log::info("connected {} -> {}", *local, *remote);

    // 3. 异步发送
    auto send_result = co_await net::send(socket, async::buffer(MESSAGE));
    if (!send_result) {
        log::error("send failed: {}", send_result.error());
        co_return;
    }

    log::info("sent {} bytes", *send_result);

    // 4. 异步接收回显
    std::string buf(MESSAGE.size(), '\0');
    auto recv_result = co_await net::receive(socket, async::buffer(buf));
    if (!recv_result) {
        log::error("recv failed: {}", recv_result.error());
        co_return;
    }

    if (*recv_result == 0) {
        log::warning("server closed connection before replying");
        co_return;
    }
    
    log::info("received {} bytes: {}", *recv_result,
              std::string_view{ buf.data(), *recv_result });
}

} // namespace

int main()
{
    async::run(client);
}
