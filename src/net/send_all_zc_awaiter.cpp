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

auto SendAllZCAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    handle_ = handle;
    return arm_zc_write();
}

auto SendAllZCAwaiter::await_resume() -> std::expected<resume_type, std::error_code>
{
    if (error_code_ != 0)
        return unexpected_system_error(error_code_);

    return bytes_written_;
}

void SendAllZCAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    // 非 NOTIF 的 CQE 携带发送结果（字节数或错误码）。
    // NOTIF CQE 表示内核已释放对缓冲区的引用，其 result 字段无意义，
    // 不应更新 error_code_ 或 bytes_written_。
    if (!(flags & IORING_CQE_F_NOTIF))
        set_result(result, flags);

    // 没有 IORING_CQE_F_MORE 标志，说明这是本次 arm_zc_write() 的最后一个 CQE：
    // 要么发送失败（不会再有 NOTIF），要么 NOTIF 已到达（缓冲区已释放）。
    // 无论哪种情况，都在此决定下一步动作。
    if (!(flags & IORING_CQE_F_MORE)) {
        context_.untrack(this);

        if (is_canceling_ || error_code_ != 0 || buffer_.empty()) {
            if (is_canceling_ && error_code_ == 0)
                error_code_ = ECANCELED;

            resume(handle_, result, flags);
        }
        else if (!arm_zc_write()) {
            resume(handle_, 0, 0);
        }
    }
}

auto SendAllZCAwaiter::arm_zc_write() noexcept -> bool
{
    if (auto* sqe = context_.sqe()) {
        ::io_uring_prep_send_zc(sqe, socket_, buffer_.data(), buffer_.size(), 0, 0);
        ::io_uring_sqe_set_data(sqe, this);

        context_.track(this);
        return true;
    }

    error_code_ = EAGAIN;
    return false;
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
