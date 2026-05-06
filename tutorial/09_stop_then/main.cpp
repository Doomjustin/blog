#include <stop_token>
#include <thread>

#include <blog.h>

namespace {

auto demo(std::stop_token token) -> async::Task<>
{
    using namespace std::chrono_literals;

    log::info("starting 3-second cancellable sleep");

    auto result = co_await async::stop_then(
        async::sleep_for(3s),
        token
    );

    if (!result) {
        if (result.error() == std::errc::operation_canceled)
            log::info("sleep cancelled at 200ms");
        else
            log::error("stop_then sleep failed: {}", result.error());
    }
    else {
        log::info("sleep completed (should not reach here)");
    }
}

} // namespace

int main()
{
    std::stop_source stop_source;

    using namespace std::chrono_literals;

    // Worker 线程：200ms 后触发取消
    std::jthread worker{ [&stop_source] {
        std::this_thread::sleep_for(200ms);
        log::info("worker thread requesting stop");
        stop_source.request_stop();
    } };

    async::run(stop_source, demo);
    return EXIT_SUCCESS;
}
