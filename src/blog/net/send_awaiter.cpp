#include "send_awaiter.h"

#include <utility>

#include "common/exceptions.h"

namespace net {

SendAwaiter::SendAwaiter(context_type& context, int fd, std::span<const std::byte> buffer)
  : context_{ context }, 
    fd_{ fd }, 
    buffer_{ buffer }
{}

void SendAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    handle_ = handle;

    auto* sqe = context_.sqe();
    prepare(sqe);
    ::io_uring_sqe_set_data(sqe, this);

    context().track(this);
}

auto SendAwaiter::await_resume() noexcept -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return byte_sent_;
}

void SendAwaiter::prepare(::io_uring_sqe* sqe) noexcept
{
    ::io_uring_prep_send(sqe, fd_, buffer_.data(), buffer_.size(), 0);
}

void SendAwaiter::set_result(int result, std::uint32_t flags) noexcept
{
    if (result >= 0)
        byte_sent_ = static_cast<std::size_t>(result);
    else
        error_code_ = -result;
}

void SendAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    context().untrack(this);
    set_result(result, flags);

    if (handle_) {
        auto handle = std::exchange(handle_, nullptr);
        handle.resume();
    }
}

} // namespace net