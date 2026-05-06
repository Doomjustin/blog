#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto all_worker(const char* name,
                std::chrono::milliseconds work_time) -> async::Task<>
{
    log::info("{}: started", name);
    co_await async::sleep_for(work_time);
    log::info("{}: completed", name);
}

auto run() -> async::Task<>
{
    auto start = std::chrono::steady_clock::now();

    co_await async::all(
        all_worker("task-A", 120ms),
        all_worker("task-B", 400ms),
        all_worker("task-C", 260ms)
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    log::info("all done (elapsed {}ms)", elapsed.count());
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
