#include <chrono>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto demo() -> async::Task<>
{
    log::info("timeout scenario: 5-second sleep with 1-second timeout");

    auto result = co_await async::timeout(
        async::sleep_for(5s),
        1s
    );

    if (!result) {
        if (result.error() == std::errc::timed_out)
            log::info("timed out at 1s (as expected)");
        else
            log::error("error: {}", result.error());
    }
    else {
        log::info("sleep completed (unexpected)");
    }
}

} // namespace

int main()
{
    async::run(demo);
    return EXIT_SUCCESS;
}
