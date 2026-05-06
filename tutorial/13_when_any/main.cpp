#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

/// 演示 1：两个 endpoint 竞速，取先返回的
///
/// when_any 并发提交所有操作，第一个完成的"胜出"：
///   - 返回胜者的结果（std::expected<void, error_code>）
///   - 自动取消其余未完成的操作
auto demo_any_endpoints() -> async::Task<>
{
    log::info("=== demo 1: compare two endpoints ===");
    auto start = std::chrono::steady_clock::now();

    // 模拟两个 endpoint：A 慢（300ms），B 快（100ms）
    // when_any 取先完成的，另一个被自动取消
    auto result = co_await async::when_any(
        async::sleep_for(300ms),  // endpoint A
        async::sleep_for(100ms)   // endpoint B（winner）
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (result)
        log::info("winner responded in {}ms (expected ~100ms)", elapsed.count());
    else
        log::error("both failed: {}", result.error());
}

/// 演示 2：三方竞速，取最快的
///
/// 返回值是胜者的 expected<void, error_code>，不是 tuple。
/// 所有参数的 resume_type 必须相同（此处均为 void）。
auto demo_any_three() -> async::Task<>
{
    log::info("\n=== demo 2: compare three endpoints ===");
    auto start = std::chrono::steady_clock::now();

    // 三个"请求"，速度不同
    auto result = co_await async::when_any(
        async::sleep_for(500ms),   // 慢
        async::sleep_for(100ms),   // 最快（winner）
        async::sleep_for(300ms)    // 中等
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (result)
        log::info("fastest responded in {}ms (expected ~100ms)", elapsed.count());
    else
        log::error("error: {}", result.error());
}

/// 演示 3：胜者失败时的行为
///
/// 如果最先返回的操作失败，when_any 照样返回该失败结果，
/// 并取消其余操作。调用者通过检查 expected 来判断是否需要回退。
auto demo_winner_fails() -> async::Task<>
{
    log::info("\n=== demo 3: first to complete fails ===");
    auto start = std::chrono::steady_clock::now();

    // endpoint A：200ms 后超时失败
    // endpoint B：500ms 后正常完成
    // A 先完成（以失败告终），when_any 返回 A 的失败结果并取消 B
    auto result = co_await async::when_any(
        async::timeout(async::sleep_for(5s), 200ms),  // A: 先完成，但超时失败
        async::sleep_for(500ms)                        // B: 慢，被取消
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (!result)
        log::info("first to finish failed with {} at {}ms (expected ~200ms, timed_out)",
            result.error(), elapsed.count());
    else
        log::info("unexpected success");
}

} // namespace

int main()
{
    async::run(demo_any_endpoints);
    async::run(demo_any_three);
    async::run(demo_winner_fails);
    return EXIT_SUCCESS;
}
