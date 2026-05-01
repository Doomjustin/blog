#include "receive_awaiter.h"

#include "common/exceptions.h"

namespace net {

ReceiveAwaiter::ReceiveAwaiter(context_type& context, int fd, std::span<std::byte> buffer)
  : context_{ context }, 
    fd_{ fd }, 
    buffer_{ buffer }
{}

void ReceiveAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    handle_ = handle;

    auto* sqe = context_.sqe();
    prepare(sqe);
    ::io_uring_sqe_set_data(sqe, this);
    
    context().track(this);
}

auto ReceiveAwaiter::await_resume() noexcept -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return byte_read_;
}

void ReceiveAwaiter::prepare(::io_uring_sqe* sqe) noexcept
{
    ::io_uring_prep_recv(sqe, fd_, buffer_.data(), buffer_.size(), 0);
}

void ReceiveAwaiter::set_result(int result, std::uint32_t flags) noexcept
{
    if (result >= 0)
        byte_read_ = static_cast<std::size_t>(result);
    else
        error_code_ = -result;
}

void ReceiveAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    context().untrack(this);

    set_result(result, flags);

    if (handle_) {
        auto handle = std::exchange(handle_, nullptr);
        handle.resume();
    }
}

} // namespace net