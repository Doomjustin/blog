#include "receive_all_awaiter.h"

#include <common.h>

namespace net {

ReceiveAllAwaiter::ReceiveAllAwaiter(context_type& context, int socket, std::span<std::byte> buffer)
    : context_{ context },
    socket_{ socket },
    buffer_{ buffer }
{}

void ReceiveAllAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    handle_ = handle;
    arm_read();
}

auto ReceiveAllAwaiter::await_resume() -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return bytes_read_;
}

void ReceiveAllAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    context().untrack(this);

    set_result(result, flags);

    if (is_canceling_ || error_code_ != 0 || buffer_.empty()) {
        if (is_canceling_ && error_code_ == 0)
            error_code_ = ECANCELED;

        resume(handle_, result, flags);
    }
    else {
        arm_read();
    }
}

void ReceiveAllAwaiter::arm_read() noexcept
{
    auto* sqe = context_.sqe();

    ::io_uring_prep_recv(sqe, socket_, buffer_.data(), buffer_.size(), 0);
    ::io_uring_sqe_set_data(sqe, this);
 
    context().track(this);
}

void ReceiveAllAwaiter::set_result(int result, std::uint32_t flags) noexcept
{
    if (result > 0) {
        bytes_read_ += static_cast<std::size_t>(result);
        buffer_ = buffer_.subspan(result);
    }
    else if (result == 0) {
        error_code_ = ECONNABORTED;
    }
    else {
        error_code_ = -result;
    }
}

} // namespace net