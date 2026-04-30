#include "write_all_awaiter.h"

#include "common/exceptions.h"

namespace async {

WriteAllAwaiter::WriteAllAwaiter(context_type& context, int socket, std::span<const std::byte> buffer)
  : context_{ context }, 
    socket_{ socket }, 
    buffer_{ buffer }
{}

void WriteAllAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    handle_ = handle;
    arm_write();
}

auto WriteAllAwaiter::await_resume() -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return bytes_written_;
}

void WriteAllAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    set_result(result, flags);

    if (error_code_ != 0 || buffer_.empty())
        resume(handle_, result, flags);
    else
        arm_write();
}

void WriteAllAwaiter::arm_write() noexcept
{
    auto* sqe = context_.sqe();

    ::io_uring_prep_send(sqe, socket_, buffer_.data(), buffer_.size(), 0);
    ::io_uring_sqe_set_data(sqe, this);
}

void WriteAllAwaiter::set_result(int result, std::uint32_t flags) noexcept
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

} // namespace async