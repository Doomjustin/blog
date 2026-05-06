#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto simple_worker(const char* name,
                   std::chrono::milliseconds work_time,
                   std::stop_token& token) -> async::Task<>
{
    log::info("{}: started", name);

    // Cooperative cancel points via stop_then.
    auto remaining = work_time;
    constexpr auto quantum = 20ms;

    while (remaining > 0ms) {
        auto step = std::min(remaining, quantum);
        auto step_result = co_await async::stop_then(async::sleep_for(step), token);
        if (!step_result) {
            if (step_result.error() == std::errc::operation_canceled) {
                log::info("{}: cancelled", name);
                co_return;
            }

            log::error("{}: step failed: {}", name, step_result.error());
            co_return;
        }

        remaining -= step;
    }

    log::info("{}: completed", name);
}

auto run() -> async::Task<>
{
    auto start = std::chrono::steady_clock::now();

    co_await async::race(
        async::task(simple_worker, "task-A", 120ms),
        async::task(simple_worker, "task-B", 400ms),
        async::task(simple_worker, "task-C", 260ms)
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    log::info("race done (elapsed {}ms)", elapsed.count());
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
