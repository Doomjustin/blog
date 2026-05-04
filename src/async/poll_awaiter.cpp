#include "poll_awaiter.h"

#include <cerrno>
#include <coroutine>
#include <utility>

#include <liburing.h>

#include <common.h>
#include <io_context.h>

namespace async {

auto PollAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    handle_ = handle;

    if (auto* sqe = context_.sqe()) {
        prepare(sqe);
        ::io_uring_sqe_set_data(sqe, this);

        context().track(this);
        return true;
    }

    error_code_ = EAGAIN;
    return false;
}

auto PollAwaiter::await_resume() -> std::expected<void, std::error_code>
{
    if (error_code_)
        return unexpected_system_error(error_code_);

    return {};
}

void PollAwaiter::prepare(::io_uring_sqe* sqe) noexcept
{
    ::io_uring_prep_poll_add(sqe, fd_, events_);
}

void PollAwaiter::set_result(int result, [[maybe_unused]] std::uint32_t flags) noexcept
{
    error_code_ = result < 0 ? -result : 0;
}

void PollAwaiter::complete(int res, std::uint32_t flags) noexcept
{
    context().untrack(this);
    set_result(res, flags);
    
    if (handle_) {
        auto handle = std::exchange(handle_, nullptr);
        handle.resume();
    }
}
    
} // namespace async