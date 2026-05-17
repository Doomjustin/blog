#ifndef BLOG_ASYNC_SHIFT_TO_H
#define BLOG_ASYNC_SHIFT_TO_H

#include "io_context.h"
#include "operation.h"

namespace async {

/// @brief 将当前 coroutine 切换到目标 IOContext 的 awaiter。
class ShiftToAwaiter : public Operation {
private:
    IOContext& target_;

public:
    /// @brief 构造一个目标上下文切换 awaiter。
    /// @param[in] context 目标 IOContext。
    explicit ShiftToAwaiter(IOContext& context) noexcept
      : target_{ context }
    {}

    /// @brief 若已在目标线程则无需挂起。
    /// @return true 表示可立即继续执行。
    constexpr auto await_ready() const noexcept -> bool
    {
        return target_.is_owner_thread();
    }

    /// @brief 挂起当前 coroutine 并投递到目标 IOContext。
    /// @tparam Promise 当前 coroutine 的 promise 类型。
    /// @param[in] handle 当前 coroutine 句柄。
    /// @return `noop_coroutine`，等待目标线程恢复。
    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> std::coroutine_handle<>
    {
        this->handle = handle;

        if constexpr (requires { handle.promise().context; })
            handle.promise().context = &target_;

        target_.post(this);
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

/// @brief 构造 `shift_to` awaiter。
/// @param[in] context 目标 IOContext。
/// @return 对应的 `ShiftToAwaiter`。
inline auto shift_to(IOContext& context) -> ShiftToAwaiter
{
    return ShiftToAwaiter{ context };
}

} // namespace async

#endif // BLOG_ASYNC_SHIFT_TO_H