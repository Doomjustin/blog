#ifndef BLOG_ASYNC_SLEEP_AWAITER_H
#define BLOG_ASYNC_SLEEP_AWAITER_H

#include <chrono>
#include <coroutine>
#include <expected>
#include <system_error>

#include <liburing.h>

#include <common.h>
#include <io_context.h>
#include <operation.h>
#include <this_coroutine.h>

namespace async {

/**
 * @brief Suspend a coroutine for a specified duration via io_uring timeout.
 *
 * Submits an `IORING_OP_TIMEOUT` SQE and resumes the calling coroutine
 * once the kernel reports the timer expiry. Typically constructed via
 * the `sleep_for()` factory rather than directly.
 */
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
    SleepAwaiter(IOContext& context, Duration d)
      : context_{ context }
    {
        using namespace std::chrono;
        timeout_.tv_sec = duration_cast<seconds>(d).count();
        timeout_.tv_nsec = duration_cast<nanoseconds>(d % 1s).count();
    }

    /**
     * @brief Construct using the current thread's context.
     *
     * Convenience overload for use with `sleep_for()` where the context
     * is obtained from `this_coroutine::context()`.
     *
     * @tparam Duration `std::chrono::duration` specialization.
     * @param d Sleep duration.
     */
    template<chrono_duration Duration>
    explicit SleepAwaiter(Duration d)
      : SleepAwaiter{ this_coroutine::context(), d }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool 
    { 
        return false; 
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept;

    auto await_resume() noexcept -> std::expected<void, std::error_code>;

    void complete(int res, std::uint32_t flags) noexcept override;

private:
    IOContext& context_;
    struct __kernel_timespec timeout_{};
    std::coroutine_handle<> handle_{ nullptr };
    int error_code_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_SLEEP_AWAITER_H