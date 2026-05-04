#include "send_all_awaiter.h"

#include <common.h>

namespace net {

SendAllAwaiter::SendAllAwaiter(context_type& context, int socket, std::span<const std::byte> buffer)
  : context_{ context }, 
    socket_{ socket }, 
    buffer_{ buffer }
{}

auto SendAllAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    handle_ = handle;
    return arm_write();
}

auto SendAllAwaiter::await_resume() -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return bytes_written_;
}

void SendAllAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    context_.untrack(this);
    set_result(result, flags);

    if (is_canceling_ || error_code_ != 0 || buffer_.empty()) {
        if (is_canceling_ && error_code_ == 0)
            error_code_ = ECANCELED;
        
        resume(handle_, result, flags);
    }
    else if (!arm_write()) {
        resume(handle_, 0, 0);
    }
}

auto SendAllAwaiter::arm_write() noexcept -> bool
{
    if (auto* sqe = context_.sqe()) {
        ::io_uring_prep_send(sqe, socket_, buffer_.data(), buffer_.size(), 0);
        ::io_uring_sqe_set_data(sqe, this);

        context_.track(this);
        return true;
    }

    error_code_ = EAGAIN;
    return false;
}

void SendAllAwaiter::set_result(int result, std::uint32_t flags) noexcept
{
    if (result > 0) {
        bytes_written_ += static_cast<std::size_t>(result);
        buffer_ = buffer_.subspan(result);
    }
    else if (result == 0) {
        error_code_ = ECONNABORTED;
    }
    else
        error_code_ = -result;
}

} // namespace net