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

/**
 * @brief Suspend a coroutine for a specified duration via io_uring timeout.
 *
 * Because this class is decoupled from any concrete `IOContext` definition,
 * the context type is a template parameter. The call site is expected to
 * provide a context whose type can be deduced, so instantiation happens
 * implicitly via `sleep_for()`.
 *
 * @tparam Context Execution context type (must provide `sqe()`).
 */
template<typename Context>
class SleepAwaiter: public Operation {
public:
    /**
     * @brief Construct and convert duration to kernel `__kernel_timespec`.
     *
     * @tparam Duration `std::chrono::duration` specialization.
     * @param context I/O context that drives this sleep.
     * @param d       Sleep duration.
     */
    template<chrono_duration Duration>
    SleepAwaiter(Context& context, Duration d)
      : context_{ context }
    {
        using namespace std::chrono;
        timeout_.tv_sec = duration_cast<seconds>(d).count();
        timeout_.tv_nsec = duration_cast<nanoseconds>(d % 1s).count();
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

        // count=0: fire purely on time expiry, not on completion count.
        ::io_uring_prep_timeout(sqe, &timeout_, 0, 0);
        ::io_uring_sqe_set_data(sqe, this);
    }

    auto await_resume() noexcept -> std::expected<void, std::error_code>
    {
        // io_uring signals a clean timeout with ETIME; treat it as success.
        if (error_code_ == ETIME || error_code_ == 0)
            return {};
        
        // Other errors (e.g. ECANCELED when cancelled externally).
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


/**
 * @brief Factory function that creates a `SleepAwaiter` for the given context and duration.
 *
 * @code
 * co_await sleep_for(ctx, std::chrono::milliseconds{500});
 * @endcode
 *
 * @tparam Context  Execution context type.
 * @tparam Duration `std::chrono::duration` specialization.
 * @param context  I/O context to use.
 * @param duration Sleep duration.
 * @return Awaiter that suspends the caller for `duration`.
 */
template<typename Context, chrono_duration Duration>
auto sleep_for(Context& context, Duration duration) -> SleepAwaiter<Context>
{
    return SleepAwaiter<Context>{ context, duration };
}

#endif // XIN_BLOG_SLEEP_FOR_H