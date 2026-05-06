#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

// ─── async::all：等所有 Task 完成 ────────────────────────────────────────────

auto worker(const char* name, std::chrono::milliseconds duration) -> async::Task<>
{
    log::info("{}: started", name);
    co_await async::sleep_for(duration);
    log::info("{}: completed", name);
}

auto demo_all() -> async::Task<>
{
    log::info("=== demo 1: async::all ===");
    auto start = std::chrono::steady_clock::now();

    co_await async::all(
        worker("A", 120ms),
        worker("B", 400ms),
        worker("C", 260ms)
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    log::info("all done in {}ms (expected ~400ms)", elapsed.count());
}

// ─── async::any：第一个完成即取消其余 ───────────────────────────────────────

/// 取消感知协程：以 20ms 为量子循环，每轮检查 stop_token。
/// any 第一个完成后调用 scope.request_stop()，其余任务在下一个量子
/// 入口的 stop_then::await_ready() 中发现 pre_stopped_=true，立即返回
/// operation_canceled，无需依赖 io_uring cancel SQE。
///
/// 注意：stop_token 必须按值（by value）传入——协程帧存储参数副本；
/// 用引用则帧持有悬空引用（UB）。
auto cancellable_worker(
    const char* name,
    std::chrono::milliseconds total,
    std::stop_token token          // ← by value
) -> async::Task<>
{
    log::info("{}: started", name);

    auto remaining = total;
    constexpr auto quantum = 20ms;

    while (remaining > 0ms) {
        auto step = std::min(remaining, quantum);
        auto result = co_await async::stop_then(async::sleep_for(step), token);
        if (!result) {
            log::info("{}: cancelled", name);
            co_return;
        }
        remaining -= step;
    }

    log::info("{}: completed", name);
}

/// 演示 2：any —— 第一个完成的 Task 发出取消信号
auto demo_any() -> async::Task<>
{
    log::info("\n=== demo 2: async::any ===");
    auto start = std::chrono::steady_clock::now();

    co_await async::any(
        async::task(cancellable_worker, "task-A", 120ms),
        async::task(cancellable_worker, "task-B", 400ms),
        async::task(cancellable_worker, "task-C", 260ms)
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    log::info("any done in {}ms (expected ~120ms)", elapsed.count());
}

/// 演示 3：when_any（I/O 级并发）vs any（Task 级并发）对比
auto demo_difference() -> async::Task<>
{
    log::info("\n=== demo 3: when_any vs any ===");

    {
        auto start = std::chrono::steady_clock::now();
        auto result = co_await async::when_any(
            async::sleep_for(300ms),
            async::sleep_for(100ms)   // winner
        );
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        log::info("when_any: done in {}ms, ok={}", ms, static_cast<bool>(result));
    }

    {
        auto start = std::chrono::steady_clock::now();
        co_await async::any(
            async::task(cancellable_worker, "fast", 100ms),
            async::task(cancellable_worker, "slow", 300ms)
        );
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        log::info("any: done in {}ms", ms);
    }
}

} // namespace

int main()
{
    async::run(demo_all);
    async::run(demo_any);
    async::run(demo_difference);
    return EXIT_SUCCESS;
}
