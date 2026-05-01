#include "send_zc_awaiter.h"

#include <liburing.h>
#include <liburing/io_uring.h>

#include "common/exceptions.h"

namespace net {

SendZCAwaiter::SendZCAwaiter(context_type& context, int fd, std::span<const std::byte> buffer)
  : context_{context }, 
    fd_{ fd }, 
    buffer_{ buffer }
{}

void SendZCAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    handle_ = handle;

    auto* sqe = context_.sqe();
    prepare(sqe);
    ::io_uring_sqe_set_data(sqe, this);

    context().track(this);
}

auto SendZCAwaiter::await_resume() noexcept -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return byte_sent_;
}

void SendZCAwaiter::prepare(::io_uring_sqe* sqe) noexcept
{
    ::io_uring_prep_send_zc(sqe, fd_, buffer_.data(), buffer_.size(), 0, 0);
}

void SendZCAwaiter::set_result(int result, std::uint32_t flags) noexcept
{
    if (result >= 0)
        byte_sent_ = static_cast<std::size_t>(result);
    else
        error_code_ = -result;
}

void SendZCAwaiter::complete(int result, std::uint32_t flags) noexcept
{   
    // 如果不是IORING_CQE_F_NOTIF，说明这个结果是send_zc的结果；
    // 如果是IORING_CQE_F_NOTIF，说明这个结果是内核通知的结果，
    // 此时我们不应该更新byte_sent_或者error_code_，
    // 而应该等到下一个complete来处理send_zc的结果
    if (!(flags & IORING_CQE_F_NOTIF))
        set_result(result, flags);

    // 只有当这个结果不是IORING_CQE_F_MORE时，我们才认为这个操作完成了；
    // 如果这个结果是IORING_CQE_F_MORE，说明这个操作还没有完成，
    // 我们需要继续等待下一个结果来完成这个操作
    if (!(flags & IORING_CQE_F_MORE)) {
        context().untrack(this);

        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }
}

} // namespace net