#include <chrono>
#include <cstdlib>
#include <expected>
#include <string_view>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto flaky_rpc(int& attempt, int fail_before_success)
    -> async::Task<std::expected<std::size_t, std::error_code>>
{
    ++attempt;
    co_await async::sleep_for(20ms);

    if (attempt <= fail_before_success)
        co_return std::unexpected(std::make_error_code(std::errc::timed_out));

    co_return static_cast<std::size_t>(attempt);
}

template<typename Backoff>
auto run_with_policy(std::string_view name,
                     Backoff backoff,
                     int fail_before_success)
    -> async::Task<>
{
    int attempt = 0;

    while (attempt < 6) {
        auto result = co_await flaky_rpc(attempt, fail_before_success);
        if (result) {
            log::info("[{}] success on attempt {}", name, *result);
            co_return;
        }

        auto ec = result.error();
        log::info("[{}] attempt {} failed: {}", name, attempt, ec);

        if (ec != std::errc::timed_out) {
            log::error("[{}] non-retryable error", name);
            co_return;
        }

        auto delay = backoff(attempt);
        log::info("[{}] backoff {}ms", name, delay.count());
        co_await async::sleep_for(delay);
    }

    log::error("[{}] retries exhausted", name);
}

auto run() -> async::Task<>
{
    co_await run_with_policy(
        "fixed",
        [](int) { return 50ms; },
        2
    );

    co_await run_with_policy(
        "exponential",
        [](int attempt) { return std::chrono::milliseconds{ 25 * (1 << attempt) }; },
        3
    );
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
