#include "write_awaiter.h"

#include <utility>

#include <exceptions.h>

namespace async {

WriteAwaiter::WriteAwaiter(context_type& context, int fd, std::span<const std::byte> buffer)
  : context_{ context },
    fd_{ fd },
    buffer_{ buffer }
{}

auto WriteAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
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

auto WriteAwaiter::await_resume() noexcept -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return bytes_written_;
}

void WriteAwaiter::prepare(::io_uring_sqe* sqe) noexcept
{
    ::io_uring_prep_write(sqe, fd_, buffer_.data(), buffer_.size(), -1);
}

void WriteAwaiter::set_result(int result, std::uint32_t flags) noexcept
{
    if (result >= 0)
        bytes_written_ = static_cast<std::size_t>(result);
    else
        error_code_ = -result;
}

void WriteAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    context().untrack(this);
    set_result(result, flags);

    if (handle_) {
        auto handle = std::exchange(handle_, nullptr);
        handle.resume();
    }
}

} // namespace async
