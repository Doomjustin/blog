#include "send_all_zc_awaiter.h"

#include <liburing.h>
#include <liburing/io_uring.h>

#include <common.h>

namespace net {

SendAllZCAwaiter::SendAllZCAwaiter(context_type& context, int socket, std::span<const std::byte> buffer)
  : context_{ context },
    socket_{ socket },
    buffer_{ buffer }
{}

void SendAllZCAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    handle_ = handle;
    arm_zc_write();
}

auto SendAllZCAwaiter::await_resume() -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return bytes_written_;
}

void SendAllZCAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    if (flags & IORING_CQE_F_NOTIF) {
        // Kernel has released its reference to the buffer slice.
        // Now safe to advance the pointer and decide whether to continue.
        context_.untrack(this);

        if (is_canceling_ || error_code_ != 0 || buffer_.empty()) {
            if (is_canceling_ && error_code_ == 0)
                error_code_ = ECANCELED;

            resume(handle_, result, flags);
        }
        else {
            arm_zc_write();
        }

        return;
    }

    // send CQE: carries the byte count; IORING_CQE_F_MORE is set, notif follows.
    set_result(result, flags);
}

void SendAllZCAwaiter::arm_zc_write() noexcept
{
    auto* sqe = context_.sqe();
    ::io_uring_prep_send_zc(sqe, socket_, buffer_.data(), buffer_.size(), 0, 0);
    ::io_uring_sqe_set_data(sqe, this);
    context_.track(this);
}

void SendAllZCAwaiter::set_result(int result, std::uint32_t flags) noexcept
{
    if (result > 0) {
        bytes_written_ += static_cast<std::size_t>(result);
        buffer_ = buffer_.subspan(static_cast<std::size_t>(result));
    }
    else if (result == 0)
        error_code_ = ECONNABORTED;
    else
        error_code_ = -result;
}

} // namespace net
