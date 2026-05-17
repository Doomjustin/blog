#ifndef BLOG_ASYNC_TIME_AWAITER_H
#define BLOG_ASYNC_TIME_AWAITER_H

#include <liburing.h>

#include "io_awaiter.h"

namespace async {

/// @brief 基于 io_uring timeout 的定时 Awaiter。
/// @details 用于 sleep_for/sleep_until 场景，结果在 await_resume 中转换为 expected。
class TimerAwaiter : public IOAwaiter<TimerAwaiter> {
private:
    ::__kernel_timespec timeout_;

    /// @brief 将 chrono duration 转换为 __kernel_timespec。
    /// @param[in] duration 输入时长。
    /// @return 转换后的内核时间结构。
    auto cast_time(chrono_duration auto duration) -> __kernel_timespec
    {
        using namespace std::chrono;
        auto ns = duration_cast<nanoseconds>(duration).count();
        return { ns / 1'000'000'000, ns % 1'000'000'000 };
    }

public:
    /// @brief 以相对时长构造定时 Awaiter。
    /// @param[in] duration 相对等待时长。
    ///
    /// 当 duration 为负值时，按零时长处理（立即就绪）。
    template<chrono_duration Duration>
    TimerAwaiter(Duration duration)
      : IOAwaiter<TimerAwaiter>{}
    {
        if (duration < Duration::zero())
            timeout_ = { .tv_sec = 0, .tv_nsec = 0 };
        else
            timeout_ = cast_time(duration);
    }

    /// @brief 以绝对时间点构造定时 Awaiter。
    /// @param[in] timepoint 目标时间点。
    ///
    /// 当 timepoint 不晚于当前时间时，按零时长处理（立即就绪）。
    template<typename Clock, typename Duration>
    TimerAwaiter(std::chrono::time_point<Clock, Duration> timepoint)
      : IOAwaiter<TimerAwaiter>{}
    {
        auto now = Clock::now();

        if (now >= timepoint)
            timeout_ = { .tv_sec = 0, .tv_nsec = 0 };
        else
            timeout_ = cast_time(timepoint - now);
    }

    /// @brief 判断是否可立即完成（零时长）。
    /// @return true 表示无需挂起；false 表示需要提交 timeout。
    auto await_ready() const noexcept -> bool
    {
        return timeout_.tv_sec == 0 && timeout_.tv_nsec == 0;
    }

    /// @brief completion 后返回结果。
    /// @return 成功时返回空 expected；失败时返回 error_code。
    ///
    /// `result == 0` 或 `result == -ETIME` 都视作定时成功完成。
    auto await_resume() noexcept -> std::expected<void, std::error_code>
    {
        if (result == 0 || result == -ETIME)
            return {};

        return unexpected_system_error(-result);
    }

    /// @brief 准备 timeout SQE。
    /// @param[in,out] sqe 需要填充的 io_uring SQE。
    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_timeout(sqe, &timeout_, 0, 0);
    }
};

} // namespace async

#endif // BLOG_ASYNC_TIME_AWAITER_H