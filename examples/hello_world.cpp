#include <cstdlib>
#include <stop_token>

#include <blog.h>

using namespace std::literals;

auto hello() -> async::Task<>
{
    log::info("before");

    auto res = co_await async::timeout(async::sleep_for(10s), 5s);
    if (!res) {
        log::error("{}", res.error());
        co_return;
    }

    log::info("after");
}

auto shutdown_monitor(std::stop_source stop_source) -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    auto res = co_await signals.async_wait();

    if (!res) {
        log::error("signal wait failed: {}", res.error());
        co_return;
    }

    log::info("received terminate signal {}", *res);
    stop_source.request_stop();
}

int main(int argc, char* argv[])
{
    async::IOContext context;
    std::stop_source stop_source;

    async::co_spawn(context, shutdown_monitor(stop_source));
    async::co_spawn(context, stop_source.get_token(), hello());

    context.run();

    return EXIT_SUCCESS;
}