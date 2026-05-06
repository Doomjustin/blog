#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto delayed_hello() -> async::Task<>
{
    log::info("before sleep");
    co_await async::sleep_for(1s);
    log::info("after sleep");
}

} // namespace

int main()
{
    async::run(delayed_hello);
}
