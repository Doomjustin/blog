#include <array>
#include <cstdlib>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 场景 1：业务逻辑层 SLA 超时
// ============================================================================
auto demo_sla_timeout() -> async::Task<>
{
    log::info("=== Demo 1: 业务逻辑超时熔断 (限制 200ms) ===");
    log::info("[App] 向上游发起请求，最大容忍耗时 200ms...");

    auto result = co_await async::timeout(async::sleep_for(5s), 200ms);

    if (!result && result.error() == std::errc::timed_out) {
        log::warning("[App] 任务响应超时 (>200ms)。");
        log::warning("[App] 底层 io_uring 已自动 Cancel 原挂起状态，防止死等。\n");
        co_return;
    }

    log::error("[App] 未预期的执行结果");
}

// ============================================================================
// 场景 2：网络 I/O 读超时（防止死连接）
// ============================================================================
auto mock_silent_server(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    net::ip::tcp::endpoint peer;
    auto accepted = co_await acceptor.async_accept(peer);
    if (!accepted)
        co_return;

    log::info("[Server] 收到客户端连接，但在读超时窗口内不发送任何数据...");
    co_await async::sleep_for(1s);
}

auto mock_fast_server(net::ip::tcp::endpoint endpoint) -> async::Task<>
{
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    net::ip::tcp::endpoint peer;
    auto accepted = co_await acceptor.async_accept(peer);
    if (!accepted)
        co_return;

    co_await async::sleep_for(50ms);

    std::array<std::byte, 2> payload{ std::byte{'O'}, std::byte{'K'} };
    auto sent = co_await net::send(*accepted, std::span{ payload.data(), payload.size() });
    if (!sent)
        log::error("[Server] 发送响应失败: {}", sent.error());
}

auto demo_network_read_timeout() -> async::Task<>
{
    log::info("=== Demo 2: 网络 I/O 读取超时 (防止死连接) ===");

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 10086 };

    async::co_spawn(mock_silent_server(endpoint));
    co_await async::sleep_for(50ms);

    auto socket = net::ip::tcp::socket{ endpoint.protocol() };
    try {
        socket.connect(endpoint);
        log::info("[Client] 已连接服务器，准备读取数据，设置 500ms 读超时...");
    }
    catch (const std::exception& ex) {
        log::error("[Client] 连接失败: {}", ex.what());
        co_return;
    }

    std::array<std::byte, 1024> buffer;
    auto result = co_await async::timeout(socket.async_receive_some(buffer), 500ms);
    if (!result && result.error() == std::errc::timed_out) {
        log::error("[Client] 读取超时 (>500ms)，判定服务器无响应。");
        log::info("[Client] 主动放弃读取；底层 io_uring 会清理悬空 Socket 读事件。\n");
        co_return;
    }

    log::error("[Client] 未预期的执行结果");
}

// ============================================================================
// 场景 3：快路径（未触发超时）
// ============================================================================
auto demo_happy_path() -> async::Task<>
{
    log::info("=== Demo 3: 快路径放行 (未触发超时) ===");
    log::info("[App] 连接快速响应服务，读取超时阈值设为 1 秒...");

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 10087 };
    async::co_spawn(mock_fast_server(endpoint));
    co_await async::sleep_for(50ms);

    auto socket = net::ip::tcp::socket{ endpoint.protocol() };
    try {
        socket.connect(endpoint);
    }
    catch (const std::exception& ex) {
        log::error("[App] 连接快速服务失败: {}", ex.what());
        co_return;
    }

    std::array<std::byte, 16> buffer;
    auto result = co_await async::timeout(socket.async_receive_some(buffer), 1s);
    if (result && *result > 0) {
        log::info("[App] 请求在阈值内成功返回，读取 {} 字节，超时监控自动清理。\n", *result);
        co_return;
    }

    log::error("[App] 未预期的执行结果: {}", result.error());
}

} // namespace

int main()
{
    async::run(demo_sla_timeout);
    async::run(demo_network_read_timeout);
    async::run(demo_happy_path);
    return EXIT_SUCCESS;
}
