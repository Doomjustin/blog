#include <cstdlib>
#include <expected>
#include <stop_token>
#include <string_view>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto classify(std::error_code ec) -> std::string_view
{
    if (ec == std::errc::operation_canceled)
        return "canceled";
    if (ec == std::errc::timed_out)
        return "timed_out";
    if (ec == std::errc::connection_reset)
        return "connection_reset";
    if (ec == std::errc::broken_pipe)
        return "broken_pipe";
    return "other";
}

auto handle(std::string_view stage, std::error_code ec) -> void
{
    if (ec == std::errc::operation_canceled) {
        log::info("[{}] category={} -> exit gracefully", stage, classify(ec));
        return;
    }

    if (ec == std::errc::timed_out) {
        log::info("[{}] category={} -> retry candidate", stage, classify(ec));
        return;
    }

    if (ec == std::errc::connection_reset || ec == std::errc::broken_pipe) {
        log::info("[{}] category={} -> close and reconnect", stage, classify(ec));
        return;
    }

    log::error("[{}] category={} -> unexpected error: {}", stage, classify(ec), ec);
}

auto run() -> async::Task<>
{
    std::stop_source stop;
    stop.request_stop();

    auto canceled = co_await async::stop_then(async::sleep_for(50ms), stop.get_token());
    if (!canceled)
        handle("stop_then", canceled.error());

    auto timed = co_await async::timeout(async::sleep_for(200ms), 50ms);
    if (!timed)
        handle("timeout", timed.error());

    auto reset = std::unexpected(std::make_error_code(std::errc::connection_reset));
    std::expected<void, std::error_code> synthetic{ reset };
    if (!synthetic)
        handle("synthetic", synthetic.error());
}

} // namespace

int main()
{
    async::run(run);
    return EXIT_SUCCESS;
}
