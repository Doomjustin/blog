#ifndef BLOG_ASYNC_SLEEP_AWAITER_H
#define BLOG_ASYNC_SLEEP_AWAITER_H

#include <chrono>
#include <coroutine>
#include <expected>
#include <system_error>

#include <liburing.h>

#include "common/chrono_duration.h"
#include "io_context.h"
#include "operation.h"

namespace async {

/**
 * @brief Suspend a coroutine for a specified duration via io_uring timeout.
 *
 * Because this class is decoupled from any concrete `IOContext` definition,
 * the context type is a template parameter. The call site is expected to
 * provide a context whose type can be deduced, so instantiation happens
 * implicitly via `sleep_for()`.
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