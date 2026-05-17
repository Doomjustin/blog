#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <variant>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

auto thread_tag() -> std::size_t
{
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

auto worker(std::string_view name, async::IOContext& home, async::IOContext& peer,
            std::chrono::milliseconds step_delay, int steps) -> async::Task<>
{
    for (int i = 1; i <= steps; ++i) {
        // 在两个 IOContext 之间交替跳转，强制覆盖多线程调度路径。
        co_await async::shift_to((i % 2 == 0) ? home : peer);

        auto res = co_await async::sleep_for(step_delay);
        if (!res) {
            if (res.error() == std::errc::operation_canceled) {
                log::warning("[{}] canceled at step {}/{} (thread={})", name, i, steps,
                             thread_tag());
                co_return;
            }

            log::error("[{}] failed at step {}/{}: {} (thread={})", name, i, steps, res.error(),
                       thread_tag());
            co_return;
        }

        log::info("[{}] step {}/{} done (thread={})", name, i, steps, thread_tag());
    }

    log::info("[{}] completed (thread={})", name, thread_tag());
}

auto demo_all(async::IOContext& home, async::IOContext& peer) -> async::Task<>
{
    log::info("=== demo all(): multi-thread wait-all ===");

    co_await async::all(worker("all-A", home, peer, 30ms, 3), worker("all-B", home, peer, 50ms, 2),
                        worker("all-C", home, peer, 20ms, 4));

    log::info("all() returned after every task completed");
}

auto demo_any(async::IOContext& home, async::IOContext& peer) -> async::Task<>
{
    log::info("=== demo any(): multi-thread win-first ===");

    co_await async::any(worker("any-fast", home, peer, 25ms, 2),
                        worker("any-mid", home, peer, 80ms, 10),
                        worker("any-slow", home, peer, 120ms, 10));

    log::info("any() returned after winner finished and losers drained");
}

auto stress_multi_thread(async::IOContext& home, async::IOContext& peer) -> async::Task<>
{
    constexpr int rounds = 120;
    log::info("=== stress any/all: {} rounds ===", rounds);

    for (int round = 1; round <= rounds; ++round) {
        co_await async::all(worker("stress-all-A", home, peer, 1ms, 2),
                            worker("stress-all-B", home, peer, 2ms, 2));

        co_await async::any(worker("stress-any-fast", home, peer, 1ms, 1),
                            worker("stress-any-mid", home, peer, 3ms, 4),
                            worker("stress-any-slow", home, peer, 5ms, 4));

        if (round % 20 == 0)
            log::info("stress progress: {}/{}", round, rounds);
    }

    log::info("stress any/all finished");
}

auto keep_context_alive(async::Channel<std::monostate>& done) -> async::Task<>
{
    auto res = co_await done.async_receive();
    if (!res)
        log::error("keep_context_alive receive failed: {}", res.error());
}

auto run_examples(async::IOContext& home, async::IOContext& peer,
                  async::Channel<std::monostate>& done) -> async::Task<>
{
    co_await demo_all(home, peer);
    co_await demo_any(home, peer);
    co_await stress_multi_thread(home, peer);

    while (!done.try_send(std::monostate{}))
        std::this_thread::yield();
}

} // namespace

int main()
{
    async::IOContext context_a;
    async::IOContext context_b;
    async::Channel<std::monostate> done{ 1 };

    async::co_spawn(context_a, run_examples(context_a, context_b, done));
    async::co_spawn(context_b, keep_context_alive(done));

    std::jthread thread_a([&] { context_a.run(); });
    std::jthread thread_b([&] { context_b.run(); });

    return EXIT_SUCCESS;
}
