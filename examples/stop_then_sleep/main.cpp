#include <cstdlib>
#include <stop_token>
#include <thread>

#include <blog.h>
#include <post.h>

namespace {

auto stop_then_sleep_demo() -> async::Task<>
{
    auto& ctx = async::this_coroutine::context();
    std::stop_source stop_source;

    using namespace std::chrono_literals;

    std::jthread worker{ [&ctx, source = stop_source]() mutable {
        std::this_thread::sleep_for(200ms);

        async::dispatch(ctx, [] { log::info("dispatch from worker thread"); });

        source.request_stop();
    } };

    log::info("waiting on cancellable sleep");

    auto result = co_await async::stop_then(async::sleep_for(3s), stop_source.get_token());
    if (!result) {
        if (result.error() == std::errc::operation_canceled)
            log::info("sleep cancelled by stop token");
        else
            log::error("stop_then sleep failed: {}", result.error());
    }
    else {
        log::info("sleep completed without cancellation");
    }

    co_return;
}

} // namespace

int main()
{
    async::run(stop_then_sleep_demo);
    return EXIT_SUCCESS;
}
