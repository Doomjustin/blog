#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

/// 演示 1：等待所有操作完成，汇集结果
///
/// when_all 并发启动所有参数，挂起直到每一个都完成（或失败），
/// 返回 tuple<expected...>，元素顺序与参数顺序一一对应。
auto demo_when_all_basic() -> async::Task<>
{
    log::info("=== demo 1: basic when_all ===");
    auto start = std::chrono::steady_clock::now();

    // 三个"请求"并发执行（用 sleep_for 模拟不同延迟的 I/O）
    auto [r0, r1, r2] = co_await async::when_all(
        async::sleep_for(300ms),   // request A: 慢
        async::sleep_for(100ms),   // request B: 快
        async::sleep_for(200ms)    // request C: 中等
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    // 总耗时取决于最慢的那个（300ms），而不是三者之和（600ms）
    log::info("all done in {}ms (expected ~300ms)", elapsed.count());

    // 每个返回值都是 std::expected<void, std::error_code>
    if (r0 && r1 && r2)
        log::info("all succeeded");
    else
        log::error("one or more failed");
}

/// 演示 2：合并两个独立操作的结果
///
/// 两个请求各自产出数据，when_all 确保二者都完成后才合并。
auto demo_merge_results() -> async::Task<>
{
    log::info("\n=== demo 2: merge results ===");
    auto start = std::chrono::steady_clock::now();

    // 并发发出两个"请求"；各自有自己的延迟
    auto [fast, slow] = co_await async::when_all(
        async::sleep_for(150ms),   // request 1
        async::sleep_for(400ms)    // request 2
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    log::info("both requests done in {}ms (expected ~400ms)", elapsed.count());

    // 只有双方都成功，才执行合并逻辑
    if (!fast) {
        log::error("request 1 failed: {}", fast.error());
        co_return;
    }
    if (!slow) {
        log::error("request 2 failed: {}", slow.error());
        co_return;
    }

    log::info("results merged successfully");
}

/// 演示 3：其中一个失败时的行为
///
/// when_all 不会因某个操作失败而中途取消其他操作；
/// 所有操作运行完毕后，返回 tuple，调用者逐一检查各项结果。
auto demo_partial_failure() -> async::Task<>
{
    log::info("\n=== demo 3: partial failure ===");

    auto [r0, r1] = co_await async::when_all(
        async::sleep_for(100ms),  // 正常完成
        async::timeout(async::sleep_for(5s), 200ms)  // 超时失败
    );

    // r0 应该成功，r1 应该返回 timed_out
    log::info("r0 ok={}, r1 ok={}", static_cast<bool>(r0), static_cast<bool>(r1));

    if (!r1) {
        log::info("r1 failed with: {} (expected timed_out)", r1.error());
    }
}

} // namespace

int main()
{
    async::run(demo_when_all_basic);
    async::run(demo_merge_results);
    async::run(demo_partial_failure);
    return EXIT_SUCCESS;
}

