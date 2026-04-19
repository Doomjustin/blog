#ifndef XIN_BLOG_SLEEP_FOR_H
#define XIN_BLOG_SLEEP_FOR_H

#include <chrono>
#include <coroutine>
#include <expected>
#include <system_error>
#include <utility>

#include <liburing.h>

#include "chrono_duration.h"
#include "exceptions.h"
#include "operation.h"

// 为了搭配不同版本的IOContext，我们将 sleep_for 的实现放在了单独的头文件中，以便在不同版本的 IOContext 中进行适当的调整。
// 但是这样一来，我们无法在头文件中拿到IOContext的定义，因此我们需要将IOContext作为模板参数传入，以便在实现中能够正确地调用相关接口。
// 这里基于这样一个前提，调用点应该能够正确地推导出IOContext的类型，从而能够正确地实例化 SleepAwaiter 类。
template<typename Context>
class SleepAwaiter: public Operation {
public:
    template<chrono_duration Duration>
    SleepAwaiter(Context& context, Duration d)
      : context_{ context }
    {
        using namespace std::chrono;
        // 转换 std::chrono 时间为内核认识的 timespec
        timeout_.tv_sec = duration_cast<seconds>(d).count();
        timeout_.tv_nsec = duration_cast<nanoseconds>(d % seconds(1)).count();
    }

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool 
    { 
        return false; 
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;
        auto* sqe = context_.sqe();

        // 提交纯超时指令，count 设为 0 表示只受时间触发
        ::io_uring_prep_timeout(sqe, &timeout_, 0, 0);
        ::io_uring_sqe_set_data(sqe, this);
    }

    auto await_resume() noexcept -> std::expected<void, std::error_code>
    {
        // io_uring 中，超时正常结束会返回 ETIME
        if (error_code_ == ETIME || error_code_ == 0)
            return {};
        
        // 其他错误（如 ECANCELED 被提前强杀）
        return unexpected_system_error(error_code_);
    }

    void complete(int res, std::uint32_t flags) noexcept override
    {
        error_code_ = -res;
        
        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

private:
    Context& context_;
    struct __kernel_timespec timeout_{};
    std::coroutine_handle<> handle_{ nullptr };
    int error_code_{ 0 };
};


template<typename Context, chrono_duration Duration>
auto sleep_for(Context& context, Duration duration) -> SleepAwaiter<Context>
{
    return SleepAwaiter<Context>{ context, duration };
}

#endif // XIN_BLOG_SLEEP_FOR_H