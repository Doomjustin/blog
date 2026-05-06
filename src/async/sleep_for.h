#ifndef BLOG_ASYNC_SLEEP_FOR_H
#define BLOG_ASYNC_SLEEP_FOR_H

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
class TimerAwaier: public SingleOperation<TimerAwaier, void> {
public:
    /**
     * @brief Construct and convert duration to kernel `__kernel_timespec`.
     *
     * @tparam Duration `std::chrono::duration` specialization.
     * @param context I/O context that drives this sleep.
     * @param d       Sleep duration.
     */
    template<chrono_duration Duration>
    TimerAwaier(context_type& context, Duration d)
      : SingleOperation<TimerAwaier, void>{ context }
    {
        using namespace std::chrono;
        auto ns = duration_cast<nanoseconds>(d).count();
        timeout_.tv_sec = ns / 1'000'000'000;
        timeout_.tv_nsec = ns % 1'000'000'000;
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
    explicit TimerAwaier(Duration d)
      : TimerAwaier{ this_coroutine::context(), d }
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


/**
 * @brief Suspend the calling coroutine for `duration` using the current thread's context.
 *
 * @code
 * co_await sleep_for(std::chrono::milliseconds{500});
 * @endcode
 *
 * @tparam Duration `std::chrono::duration` specialization.
 * @param duration Sleep duration.
 * @return Awaiter that suspends the caller for `duration`.
 */
template<chrono_duration Duration>
auto sleep_for(Duration duration) -> TimerAwaier
{
    return TimerAwaier{ duration };
}

} // namespace async

#endif // BLOG_ASYNC_SLEEP_FOR_H