#include <chrono>
#include <cstdlib>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 场景 1：Hedged Requests (高可用冗余请求)
// 典型的尾延迟（Tail Latency）优化策略。向两个副本同时发起请求，
// 只要最快的一个返回，立刻在 io_uring 层面对慢的那个下发 CANCEL SQE。
// ============================================================================
auto demo_hedged_requests() -> async::Task<>
{
    using namespace std::chrono;

    log::info("=== Demo 1: 高可用冗余请求 (Hedged Requests) ===");
    auto start = steady_clock::now();

    // 真实场景中这里是向两个 Replica 节点发起 net::receive
    auto result = co_await async::when_any(
        async::sleep_for(150ms),  // 节点 A：发生网络抖动，很慢
        async::sleep_for(50ms)    // 节点 B：网络畅通，最快返回 (Winner)
    );

    auto ms = duration_cast<milliseconds>(steady_clock::now() - start).count();

    if (result)
        log::info("[Gateway] 冗余请求胜利！仅耗时 {}ms 拿到了数据。(慢节点已被自动 Cancel)", ms);
    else
        log::error("[Gateway] 所有副本全部请求失败: {}", result.error());
}

// ============================================================================
// 场景 2：SLA 保护线 (Primary I/O vs SLA Guard)
// 主链路请求与 SLA 超时信号竞速。若主链路在时限内完成，直接使用结果；
// 否则 SLA 保护线优先触发，主链路被取消并执行降级。
// ============================================================================
auto demo_heterogeneous_multiplexing() -> async::Task<>
{
    log::info("\n=== Demo 2: SLA 抢占竞速 (Primary I/O vs SLA Guard) ===");
    log::info("[Gateway] 主链路请求进行中，同时启用 SLA 保护线...");

    auto result = co_await async::when_any(
        async::sleep_for(500ms),                    // 主链路 I/O（慢）
        async::timeout(async::sleep_for(5s), 80ms) // SLA 保护线（快，通常触发超时）
    );

    if (result)
        log::info("[Gateway] 主链路在 SLA 内返回，继续主路径处理。");
    else
        log::warning("[Gateway] SLA 保护线触发（{}），主链路被取消并执行降级。", result.error());
}

// ============================================================================
// 场景 3：Deadline 熔断 (Poison Pill)
// 长轮询请求与全局熔断 deadline 竞速。熔断信号一旦触发，
// 挂起的慢链路立即被 Cancel，整个 coroutine 以错误码快速退出。
// ============================================================================
auto demo_poison_pill() -> async::Task<>
{
    log::info("\n=== Demo 3: Deadline Poison Pill (全局熔断) ===");
    log::info("[Client] 客户端发起长轮询，请求可能挂起很久...");

    auto result = co_await async::when_any(
        async::sleep_for(10s),                           // 慢链路：可能长期挂起
        async::timeout(async::sleep_for(5s), 100ms)     // 全局控制：100ms 熔断信号
    );

    if (!result)
        log::info("[Client] 收到熔断信号（{}），长轮询已被瞬间打断。", result.error());
    else
        log::info("[Client] 长轮询正常结束。");
}

} // namespace

int main()
{
    async::run(demo_hedged_requests);
    async::run(demo_heterogeneous_multiplexing);
    async::run(demo_poison_pill);
    return EXIT_SUCCESS;
}
