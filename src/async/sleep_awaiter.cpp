#include "sleep_awaiter.h"

#include <utility>

#include <common.h>

namespace async {

auto SleepAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    handle_ = handle;

    if (auto* sqe = context_.sqe()) {
        // count=0: fire purely on time expiry, not on completion count.
        ::io_uring_prep_timeout(sqe, &timeout_, 0, 0);
        ::io_uring_sqe_set_data(sqe, this);

        context_.track(this);
        return true;
    }

    error_code_ = EAGAIN;
    return false;
}

auto SleepAwaiter::await_resume() noexcept -> std::expected<void, std::error_code>
{
    // io_uring signals a clean timeout with ETIME; treat it as success.
    if (error_code_ == ETIME || error_code_ == 0)
        return {};
    
    // Other errors (e.g. ECANCELED when cancelled externally).
    return unexpected_system_error(error_code_);
}

void SleepAwaiter::complete(int res, std::uint32_t flags) noexcept
{
    context_.untrack(this);
    error_code_ = -res;
    
    if (handle_) {
        auto handle = std::exchange(handle_, nullptr);
        handle.resume();
    }
}

} // namespace async