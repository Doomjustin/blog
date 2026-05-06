#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto run() -> async::Task<>
{
    auto start = std::chrono::steady_clock::now();

    // Race three timers concurrently.
    // All have the same resume_type (void), so when_any returns
    // std::expected<void, std::error_code> — the winner's result directly.
    // The two slower timers are cancelled once the first CQE arrives.
    auto result = co_await async::when_any(
        async::sleep_for(500ms),   // loser
        async::sleep_for(100ms),   // winner
        async::sleep_for(300ms)    // loser
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (result)
        log::info("first timer fired (elapsed {}ms, expected ~100ms)", elapsed.count());
    else
        log::error("timer error: {}", result.error());
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
