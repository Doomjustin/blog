#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto run() -> async::Task<>
{
    auto start = std::chrono::steady_clock::now();

    // Launch three timers concurrently; all run in parallel.
    // when_all suspends until every CQE has been received, then returns a
    // tuple of std::expected — one element per operand, in argument order.
    auto [r0, r1, r2] = co_await async::when_all(
        async::sleep_for(300ms),
        async::sleep_for(100ms),
        async::sleep_for(200ms)
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    // Total time is ~300 ms (the slowest), not ~600 ms (the sum).
    // Each element is std::expected<void, std::error_code>.
    if (r0 && r1 && r2)
        log::info("all timers done (elapsed {}ms, expected ~300ms)", elapsed.count());
    else
        log::error("one or more timers failed");
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
