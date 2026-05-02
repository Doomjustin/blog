#include <blog.h>

namespace {

auto task(int id, std::chrono::milliseconds delay) -> async::Task<>
{
    log::info("task {} started", id);

    co_await async::sleep_for(delay);

    log::info("task {} done after {}ms", id, delay.count());
}

auto run_all() -> async::Task<>
{
    using namespace std::chrono_literals;

    async::co_spawn(task(1, 300ms));
    async::co_spawn(task(2, 100ms));
    async::co_spawn(task(3, 200ms));

    log::info("all tasks spawned, main coroutine exiting");
    co_return;
}

} // namespace

int main()
{
    async::run(run_all);
}
