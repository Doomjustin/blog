#include <chrono>
#include <cstdlib>

#include <common/exceptions.h>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 核心范式：零开销的异步重试（Zero-Cost Async Retry）
//
// 使用模板 Action 接收任意可调用对象，编译器将其完全内联，
// 无虚函数、无堆分配，不产生 std::function 的类型擦除开销。
// ============================================================================
template<typename Action>
auto with_exponential_backoff(
    Action&& action,
    int max_retries = 3,
    std::chrono::milliseconds initial_delay = 50ms
) -> async::Task<std::expected<void, std::error_code>>
{
    auto delay = initial_delay;
    std::error_code last_ec;

    for (int attempt = 1; attempt <= max_retries; ++attempt) {
        auto result = co_await action();

        if (result) {
            log::info("[Retry] 第 {} 次尝试成功！", attempt);
            co_return result;
        }

        last_ec = result.error();

        // 错误分类：瞬态错误可重试，致命错误立刻熔断
        bool is_transient = (
            last_ec == std::errc::connection_refused ||
            last_ec == std::errc::timed_out          ||
            last_ec == std::errc::network_unreachable
        );

        if (!is_transient) {
            log::error("[Retry] 致命错误: {}，立刻熔断。", last_ec);
            co_return std::unexpected(last_ec);
        }

        if (attempt < max_retries) {
            log::warning("[Retry] 瞬态错误: {}。等待 {}ms 后进行第 {} 次重试...",
                         last_ec, delay.count(), attempt + 1);
            co_await async::sleep_for(delay);
            delay *= 2;  // 指数退避
        }
    }

    log::error("[Retry] 已达最大重试次数 ({})，最终放弃。", max_retries);
    co_return std::unexpected(last_ec);
}

// ============================================================================
// 场景 1：微服务 RPC 指数退避重试
// 模拟不稳定的下游服务：前 2 次连接被拒，第 3 次成功。
// ============================================================================
auto demo_rpc_retry() -> async::Task<>
{
    log::info("=== Demo 1: 微服务 RPC 指数退避重试 ===");

    int attempt_count = 0;

    auto flaky_rpc_call = [&]() -> async::Task<std::expected<void, std::error_code>> 
    {
        attempt_count++;
        co_await async::sleep_for(10ms);  // 模拟网络延迟

        if (attempt_count <= 2)
            co_return unexpected_system_error(std::errc::connection_refused);

        co_return std::expected<void, std::error_code>{};
    };

    auto result = co_await with_exponential_backoff(flaky_rpc_call, 5);

    if (result)
        log::info("[Gateway] 业务请求最终完成，共尝试 {} 次。", attempt_count);
    else
        log::error("[Gateway] 全部重试耗尽: {}", result.error());
}

// ============================================================================
// 场景 2：致命错误立刻熔断（permission_denied）
// 鉴权失败属于不可恢复错误，重试 1000 次也没用，必须立刻返回。
// ============================================================================
auto demo_fatal_circuit_break() -> async::Task<>
{
    log::info("\n=== Demo 2: 致命错误立刻熔断 ===");

    auto auth_fail_call = []() -> async::Task<std::expected<void, std::error_code>> 
    {
        co_await async::sleep_for(10ms);
        co_return unexpected_system_error(std::errc::permission_denied);
    };

    auto result = co_await with_exponential_backoff(auth_fail_call, 3);

    if (!result)
        log::info("[Gateway] 熔断机制生效（{}），快速向前端返回 HTTP 403。", result.error());
}

// ============================================================================
// 场景 3：纯异步协作式取消（Pure Async Graceful Cancellation）
// 用 co_spawn 在事件循环内发射后台协程触发取消，
// 不消耗任何 OS 线程——这是 TPC 架构的正确做法。
// ============================================================================
auto demo_graceful_cancellation() -> async::Task<>
{
    log::info("\n=== Demo 3: 纯异步协作式取消 ===");

    std::stop_source stop_src;

    // 发射后台协程：100ms 后模拟用户点击"取消"按钮。
    // stop_source 按值拷贝进协程帧，不存在任何生命周期风险。
    async::co_spawn([](std::stop_source src) -> async::Task<> 
    {
        co_await async::sleep_for(100ms);
        log::warning("[UI] 用户点击了取消按钮，触发 StopToken！");
        src.request_stop();
    }(stop_src));

    log::info("[Downloader] 开始下载大文件（预计需要很久）...");

    auto result = co_await async::stop_then(
        async::sleep_for(10s),
        stop_src.get_token()
    );

    if (!result) {
        if (result.error() == std::errc::operation_canceled)
            log::info("[Downloader] 收到取消信号，安全清理临时文件碎片。");
        else
            log::error("[Downloader] 发生 I/O 错误: {}", result.error());
    }
}

} // namespace

int main()
{
    async::run(demo_rpc_retry);
    async::run(demo_fatal_circuit_break);
    async::run(demo_graceful_cancellation);
    return EXIT_SUCCESS;
}
