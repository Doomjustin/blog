#include <chrono>
#include <cstdlib>
#include <stop_token>
#include <thread>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto run() -> async::Task<>
{
    std::stop_source stop;

    std::jthread timer{ [&stop] {
        std::this_thread::sleep_for(120ms);
        stop.request_stop();
    } };

    auto start_stop_then = std::chrono::steady_clock::now();
    auto canceled = co_await async::stop_then(async::sleep_for(500ms), stop.get_token());
    auto elapsed_stop_then = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_stop_then);

    if (!canceled)
        log::info("stop_then result={} elapsed={}ms", canceled.error(), elapsed_stop_then.count());

    auto start_timeout = std::chrono::steady_clock::now();
    auto timed = co_await async::timeout(async::sleep_for(500ms), 200ms);
    auto elapsed_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_timeout);

    if (!timed)
        log::info("timeout result={} elapsed={}ms", timed.error(), elapsed_timeout.count());
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
