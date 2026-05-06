#include <blog.h>

namespace {

auto hello() -> async::Task<>
{
    log::info("hello from coroutine");
    co_return;
}

} // namespace

int main()
{
    async::run(hello);
}
