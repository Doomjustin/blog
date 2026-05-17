#ifndef BLOG_ASYNC_TIMEOUT_AWAITER_H
#define BLOG_ASYNC_TIMEOUT_AWAITER_H

#include <coroutine>
#include <cstdint>

#include <liburing.h>

#include <common/common.h>

#include "io_context.h"
#include "operation.h"

namespace async {

/// @brief 基于 io_uring link_timeout 的超时包装。
/// @tparam Awaitable 需要提供 prepare/track/cancel 语义的 Operation 类型。
template<typename Awaitable>
class TimeoutAwaiter : public Operation {
private:
    struct Timer : public Operation {
        TimeoutAwaiter<Awaitable>* owner;

        void resume(int result, std::uint32_t flags) override
        {
            if (result == -ETIME)
                owner->is_timeout_ = true;

            if (--owner->pending_cqes_ == 0)
                std::exchange(owner->handle, nullptr).resume();
        }
    };

    Awaitable inner_;
    ::__kernel_timespec timeout_;

    Timer dummy_;
    int pending_cqes_{ 2 };
    bool is_timeout_{ false };

    void release(::io_uring_sqe* sqe) noexcept
    {
        if (sqe) {
            ::io_uring_prep_nop(sqe);
            ::io_uring_sqe_set_data(sqe, nullptr);
        }
    }

public:
    /// @brief 构造 TimeoutAwaiter，并将 timeout 转为 `__kernel_timespec`。
    /// @tparam Duration 满足 `chrono_duration` 的时长类型。
    /// @param[in] awaitable 被包装的 operation。
    /// @param[in] timeout 超时时长。
    template<chrono_duration Duration>
    TimeoutAwaiter(Awaitable&& awaitable, Duration timeout)
      : inner_{ std::move(awaitable) }
    {
        using namespace std::chrono;

        auto ns = duration_cast<nanoseconds>(timeout).count();
        timeout_.tv_sec = ns / 1'000'000'000;
        timeout_.tv_nsec = ns % 1'000'000'000;
    }

    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /// @brief 注册 inner SQE 与 link_timeout SQE。
    /// @param[in] handle 当前 coroutine continuation。
    /// @param[in,out] context 负责分配与追踪 SQE 的 IOContext。
    /// @return `std::noop_coroutine()` 等待 CQE；SQE 不足时返回 `handle` 立即重试。
    auto await_suspend(std::coroutine_handle<> handle, IOContext& context) noexcept
        -> std::coroutine_handle<>
    {
        this->handle = handle;
        dummy_.owner = this;

        auto* inner_sqe = context.sqe();
        auto* timeout_sqe = context.sqe();

        while (!inner_sqe || !timeout_sqe) {
            release(inner_sqe);
            release(timeout_sqe);

            this->result = -EAGAIN;
            return handle;
        }

        inner_.prepare(inner_sqe);
        inner_sqe->flags |= IOSQE_IO_LINK;
        inner_.parent = this;
        ::io_uring_prep_link_timeout(timeout_sqe, &timeout_, 0);

        context.track(inner_sqe, &inner_);
        context.track(timeout_sqe, &dummy_);

        this->id = inner_.id;

        return std::noop_coroutine();
    }

    /// @brief 恢复时返回 timeout 或 inner 的结果。
    /// @return timeout 时返回 `unexpected(timed_out)`，否则透传 `inner_.await_resume()`。
    auto await_resume() noexcept -> decltype(auto)
    {
        using Result = decltype(inner_.await_resume());

        if (is_timeout_)
            return Result{ std::unexpect, std::make_error_code(std::errc::timed_out) };

        return inner_.await_resume();
    }

    /// @brief 接收 inner completion。
    /// @param[in] result inner completion result。
    /// @param[in] flags inner completion flags。
    /// @return 无。
    void resume(int result, std::uint32_t flags) override
    {
        this->result = result;
        this->flags = flags;

        if (--pending_cqes_ == 0)
            std::exchange(handle, nullptr).resume();
    }

    /// @brief 取消 link_timeout 节点。
    /// @param[in,out] context 用于提交 timeout_remove SQE 的 IOContext。
    /// @return 无。
    void cancel(IOContext& context) noexcept
    {
        // timeout_remove 比默认的 ASYNC_CANCEL 更适合，
        // 不过默认的 ASYNC_CANCEL 也能正常工作（因为timeout_remove 失败时会退化为普通取消）。
        // 所以这里其实可以不要，这里想用cancel测试一下切面是否生效。
        if (auto* sqe = context.sqe()) {
            ::io_uring_prep_timeout_remove(sqe, this->id, 0);
            ::io_uring_sqe_set_data64(sqe, 0);
            ::io_uring_submit(context.ring());
        }
    }
};

} // namespace async

#endif // BLOG_ASYNC_TIMEOUT_AWAITER_H