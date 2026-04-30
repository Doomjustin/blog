#include "read_all_awaiter.h"

#include "common/exceptions.h"

namespace async {

ReadAllAwaiter::ReadAllAwaiter(context_type& context, int socket, std::span<std::byte> buffer)
    : context_{ context },
    socket_{ socket },
    buffer_{ buffer }
{}

void ReadAllAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    handle_ = handle;
    arm_read();
}

auto ReadAllAwaiter::await_resume() -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return bytes_read_;
}

void ReadAllAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    set_result(result, flags);

    if (error_code_ != 0 || buffer_.empty())
        resume(handle_, result, flags);
    else
        arm_read();
}

void ReadAllAwaiter::arm_read() noexcept
{
    auto* sqe = context_.sqe();

    ::io_uring_prep_recv(sqe, socket_, buffer_.data(), buffer_.size(), 0);
    ::io_uring_sqe_set_data(sqe, this);
}

void ReadAllAwaiter::set_result(int result, std::uint32_t flags) noexcept
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

} // namespace async