#include <chrono>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

/// 重试策略：最多重试 N 次，每次延长延迟
auto retry_with_backoff(
    std::function<async::Task<std::expected<void, std::error_code>>()> op,
    std::size_t max_retries = 3,
    std::chrono::milliseconds initial_delay = 100ms
) -> async::Task<std::expected<void, std::error_code>>
{
    std::error_code last_error;
    auto delay = initial_delay;

    for (std::size_t attempt = 0; attempt <= max_retries; ++attempt) {
        auto result = co_await op();

        if (result) {
            log::info("attempt {}: success", attempt + 1);
            co_return std::expected<void, std::error_code>{};
        }

        last_error = result.error();

        // 是否可重试的错误
        bool is_retriable = (
            last_error == std::errc::connection_refused ||
            last_error == std::errc::connection_reset ||
            last_error == std::errc::timed_out
        );

        log::warning("attempt {}: error {} ({})",
            attempt + 1,
            last_error,
            is_retriable ? "retriable" : "fatal"
        );

        if (!is_retriable || attempt == max_retries) {
            co_return std::unexpected(last_error);
        }

        log::info("retrying after {} ms", delay.count());
        co_await async::sleep_for(delay);
        delay *= 2;  // exponential backoff
    }

    co_return std::unexpected(last_error);
}

/// 演示 1：区分错误类型
auto demo_error_classification() -> async::Task<>
{
    log::info("=== demo 1: error classification ===");

    {
        log::info("\ncase 1: operation_canceled (from stop_then)");
        std::stop_source stop_src;

        // 模拟 100ms 后取消
        std::jthread cancel_thread{ [&stop_src] {
            std::this_thread::sleep_for(100ms);
            stop_src.request_stop();
        } };

        auto result = co_await async::stop_then(
            async::sleep_for(5s),
            stop_src.get_token()
        );

        if (!result) {
            if (result.error() == std::errc::operation_canceled)
                log::info("error: operation_canceled (expected)");
            else
                log::error("error: {} (unexpected)", result.error());
        }
    }

    {
        log::info("\ncase 2: timed_out (from timeout)");
        auto result = co_await async::timeout(
            async::sleep_for(5s),
            1s
        );

        if (!result) {
            if (result.error() == std::errc::timed_out)
                log::info("error: timed_out (expected)");
            else
                log::error("error: {} (unexpected)", result.error());
        }
    }
}

/// 演示 2：重试策略与错误分类
auto demo_retry_strategy() -> async::Task<>
{
    log::info("\n=== demo 2: retry strategy ===");

    std::size_t attempt_count = 0;

    // 模拟一个操作：前 2 次连接被拒，第 3 次成功
    auto flaky_operation = [&attempt_count]() -> async::Task<std::expected<void, std::error_code>>
    {
        attempt_count++;

        if (attempt_count <= 2) {
            co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
        }

        co_return std::expected<void, std::error_code>{};
    };

    auto result = co_await retry_with_backoff(flaky_operation, 5, 50ms);

    if (result) {
        log::info("overall success after {} attempts", attempt_count);
    }
    else {
        log::error("overall failure: {}", result.error());
    }
}

/// 演示 3：不可重试错误（协作取消）
auto demo_fatal_errors() -> async::Task<>
{
    log::info("\n=== demo 3: non-retriable errors ===");

    log::info("\ncase: operation_canceled should not retry");

    std::size_t retry_count = 0;
    std::stop_source stop_src;

    // 立即取消
    stop_src.request_stop();

    auto operation = [&retry_count, &stop_src]() -> async::Task<std::expected<void, std::error_code>>
    {
        retry_count++;
        auto result = co_await async::stop_then(
            async::sleep_for(100ms),
            stop_src.get_token()
        );
        co_return result ? std::expected<void, std::error_code>{} : std::unexpected(result.error());
    };

    auto result = co_await retry_with_backoff(operation, 5, 10ms);

    log::info("total retry attempts: {} (should be 1, no retry on operation_canceled)",
        retry_count);
}

} // namespace

int main()
{
    async::run(demo_error_classification);
    async::run(demo_retry_strategy);
    async::run(demo_fatal_errors);

    return EXIT_SUCCESS;
}
