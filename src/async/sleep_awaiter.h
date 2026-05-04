#ifndef BLOG_ASYNC_SLEEP_AWAITER_H
#define BLOG_ASYNC_SLEEP_AWAITER_H

#include <chrono>
#include <expected>
#include <system_error>

#include <liburing.h>

#include <common.h>
#include <single_operation.h>
#include <this_coroutine.h>

namespace async {

/**
 * @brief Suspend a coroutine for a specified duration via io_uring timeout.
 *
 * Submits an `IORING_OP_TIMEOUT` SQE and resumes the calling coroutine
 * once the kernel reports the timer expiry. Typically constructed via
 * the `sleep_for()` factory rather than directly.
 */
class SleepAwaiter: public SingleOperation<SleepAwaiter, void> {
public:
    /**
     * @brief Construct and convert duration to kernel `__kernel_timespec`.
     *
     * @tparam Duration `std::chrono::duration` specialization.
     * @param context I/O context that drives this sleep.
     * @param d       Sleep duration.
     */
    template<chrono_duration Duration>
    SleepAwaiter(context_type& context, Duration d)
      : SingleOperation<SleepAwaiter, void>{ context }
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

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        // count=0: fire purely on time expiry, not on completion count.
        ::io_uring_prep_timeout(sqe, &timeout_, 0, 0);
    }

    /**
     * @brief Return success on both clean expiry (ETIME) and normal completion.
     *
     * io_uring signals a clean timeout with `ETIME`; all other non-zero
     * `error_code_` values (e.g. `ECANCELED`) are forwarded as errors.
     */
    auto await_resume() noexcept -> std::expected<void, std::error_code>
    {
        if (error_code_ == ETIME || error_code_ == 0)
            return {};

        return unexpected_system_error(error_code_);
    }

private:
    struct __kernel_timespec timeout_{};
};

} // namespace async

#endif // BLOG_ASYNC_SLEEP_AWAITER_H
