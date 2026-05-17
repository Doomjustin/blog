#ifndef BLOG_ASYNC_TIMEOUT_WRAPPER_H
#define BLOG_ASYNC_TIMEOUT_WRAPPER_H

#include <coroutine>

#include <liburing.h>

#include <common/common.h>

#include "io_context.h"
#include "task.h"

namespace async {

/// @brief 基于 stop_token 与 timer task 的超时组合器。
/// @details 采用 win-first 语义：cancel 先到会抑制 timeout；timeout 先到返回 timed_out。
/// @tparam Awaitable 当前实现要求 `Awaitable` 继承 `Awaiter`。
/// @tparam Duration 满足 `chrono_duration` concept 的时长类型。
template<typename Awaitable, chrono_duration Duration>
class TimeoutWrapper {
private:
    /// @brief stop_source_ 回调：将 stop 请求转发到 `inner_.cancel(context_)`。
    struct CancelFn {
        TimeoutWrapper* owner;

        void operator()() noexcept
        {
            owner->inner_.cancel(owner->context_);
        }
    };

    std::stop_source stop_source_;
    IOContext* context_{ nullptr };
    bool is_timeout_{ false };
    bool is_canceled_{ false };

    Awaitable inner_;
    Duration timeout_;

    /// @details `std::stop_callback` 不可移动，因此以 `unique_ptr` 持有。
    std::unique_ptr<std::stop_callback<CancelFn>> stop_callback_;

    /// @brief 独立 timer 任务：等待 timeout 后触发 stop 请求。
    /// @param[in] combinator 当前组合器对象。
    /// @return 可由 `co_spawn` 调度的 `Task<>`。
    static auto timer(TimeoutWrapper* combinator) -> Task<>
    {
        co_await sleep_for(combinator->timeout_);

        if (combinator->is_canceled_)
            co_return;

        combinator->is_timeout_ = true;
        combinator->stop_source_.request_stop();
    }

public:
    /// @brief 构造 TimeoutWrapper。
    /// @param[in] awaitable 被包装的 inner awaiter。
    /// @param[in] timeout 超时时长。
    TimeoutWrapper(Awaitable&& awaitable, Duration timeout)
      : inner_{ std::forward<Awaitable>(awaitable) }
      , timeout_{ timeout }
    {}

    constexpr auto await_ready() const noexcept -> bool
    {
        return inner_.await_ready();
    }

    /// @brief 注册 stop 回调并启动 timer 任务。
    /// @param[in] handle 当前 coroutine continuation。
    /// @param[in,out] context 执行取消与调度的 IOContext。
    /// @return inner 对应的 `await_suspend` 返回值。
    auto await_suspend(std::coroutine_handle<> handle, IOContext& context) noexcept
        -> std::coroutine_handle<>
    {
        this->context_ = &context;

        stop_callback_ = std::make_unique<std::stop_callback<CancelFn>>(stop_source_.get_token(),
                                                                        CancelFn{ this });
        co_spawn(context, stop_source_.get_token(), timer(this));

        if constexpr (requires { inner_.await_suspend(handle, context); })
            return inner_.await_suspend(handle, context);
        else
            return inner_.await_suspend(handle);
    }

    /// @brief 按 win-first 语义返回最终结果。
    /// @return timeout 时返回 `unexpected(timed_out)`；cancel 时返回
    /// `unexpected(operation_canceled)`；否则透传 `inner_.await_resume()`。
    auto await_resume() noexcept -> decltype(auto)
    {
        using Result = decltype(inner_.await_resume());

        if (is_timeout_)
            return Result{ std::unexpect, std::make_error_code(std::errc::timed_out) };

        if (is_canceled_)
            return Result{ std::unexpect, std::make_error_code(std::errc::operation_canceled) };

        return inner_.await_resume();
    }

    /// @brief 响应外部 cancel 请求并触发 stop 流程。
    /// @param[in,out] context 为统一签名保留。
    /// @return 无。
    void cancel(IOContext& context) noexcept
    {
        is_canceled_ = true;
        stop_source_.request_stop();
    }
};

} // namespace async

#endif // BLOG_ASYNC_TIMEOUT_WRAPPER_H