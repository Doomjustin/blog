#ifndef BLOG_ASYNC_TIMEOUT_H
#define BLOG_ASYNC_TIMEOUT_H

#include <cerrno>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

#include <liburing.h>

#include <common/common.h>
#include <async/operation.h>
#include <async/single_operation.h>
#include <async/sleep_for.h>
#include <async/when_any.h>

namespace async {

/**
 * @brief Wrap an inner io_uring operation with a linked timeout SQE.
 *
 * Uses `IOSQE_IO_LINK` to chain the inner operation SQE with a
 * `io_uring_prep_link_timeout` SQE. The coroutine is resumed only after
 * both CQEs arrive so neither result is lost. If the timeout fires first,
 * `await_resume` returns `std::errc::timed_out`.
 *
 * @tparam InnerOperation Operation type satisfying `single_shot_only_operation`.
 */
template<single_shot_operation InnerOperation>
class TimeoutAwaiter: public CancelableOperation {
public:
    using resume_type = typename InnerOperation::resume_type;

    /**
     * @brief Construct from an inner operation and a timeout duration.
     *
     * @tparam Duration `std::chrono::duration` specialization.
     * @param operation Inner operation to wrap; moved into this awaiter.
     * @param timeout   Maximum allowed duration for the inner operation.
     */
    template<chrono_duration Duration>
    TimeoutAwaiter(InnerOperation&& operation, Duration timeout)
      : inner_operation_{ std::forward<InnerOperation>(operation) }
    {
        using namespace std::chrono;

        auto ns = duration_cast<nanoseconds>(timeout).count();
        timeout_.tv_sec = ns / 1'000'000'000;
        timeout_.tv_nsec = ns % 1'000'000'000;
    }

    [[nodiscard]]
    constexpr auto await_ready() const noexcept
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;

        auto* io_sqe = context().sqe();
        auto* timeout_sqe = context().sqe();
        if (!io_sqe || !timeout_sqe) {
            sanitize_sqe(io_sqe);
            sanitize_sqe(timeout_sqe);

            result_ = -EAGAIN;
            return false;
        }

        inner_operation_.prepare(io_sqe);
        io_sqe->flags |= IOSQE_IO_LINK;
        ::io_uring_sqe_set_data(io_sqe, this);

        ::io_uring_prep_link_timeout(timeout_sqe, &timeout_, 0);
        ::io_uring_sqe_set_data(timeout_sqe, this);

        context().track(this);
        return true;
    }

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (is_timed_out_)
            return unexpected_system_error(std::errc::timed_out);

        if (result_ < 0)
            return unexpected_system_error(-result_);

        if constexpr (!std::is_void_v<resume_type>)
            inner_operation_.set_result(result_, 0);

        return inner_operation_.await_resume();
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result == -ETIME)
            is_timed_out_ = true;
        else if (result != -ECANCELED)
            result_ = result;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (--pending_cqes_ == 0) {
            context().untrack(this);
            this->resume(handle_, result_, flags);
        }
    }

    void cancel() noexcept override
    {
        context().cancel(this);
    }
    
    auto context() noexcept -> decltype(std::declval<InnerOperation&>().context())
    {
        return inner_operation_.context();
    }

private:
    static void sanitize_sqe(::io_uring_sqe* sqe) noexcept
    {
        if (!sqe)
            return;

        ::io_uring_prep_nop(sqe);
        ::io_uring_sqe_set_data(sqe, nullptr);
    }

    InnerOperation inner_operation_;
    __kernel_timespec timeout_;

    std::coroutine_handle<> handle_;
    // Both CQEs (inner op + timeout) must complete before resuming the coroutine.
    // If the timeout CQE arrives first, we still wait for the inner op CQE to
    // avoid losing its result.
    int pending_cqes_{ 2 };
    bool is_timed_out_{ false };
    int result_{ -ECANCELED };
};


/**
 * @brief Thin awaiter wrapping `WhenAnyAwaiter<Op, SleepAwaiter>`.
 *
 * Adapts the variant result into a flat `std::expected`:
 * - index 0 (Op wins)    → forward the inner result.
 * - index 1 (sleep wins) → return `std::errc::timed_out`.
 */
template<cancelable_operation Op>
class TimeoutWrapper: public CancelableOperation {
public:
    using resume_type = typename Op::resume_type;

    TimeoutWrapper(Op&& op, TimerAwaier sleep)
      : inner_{ std::move(op), std::move(sleep) }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        inner_.parent = this;
        return inner_.await_suspend(handle);
    }

    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (inner_.winner() == 1)
            return unexpected_system_error(std::errc::timed_out);
        // Op won. In the homogeneous (void) case await_resume() returns
        // expected<void>; in the heterogeneous case it returns a variant —
        // pull index 0 in that case.
        if constexpr (std::is_void_v<resume_type>) {
            return inner_.await_resume();
        } else {
            auto result = inner_.await_resume();
            return std::get<0>(result);
        }
    }

    auto context() noexcept -> decltype(auto) { return inner_.context(); }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        this->resume(handle_, result, flags);
    }

    void cancel() noexcept override
    {
        inner_.cancel();
    }

private:
    WhenAnyAwaiter<Op, TimerAwaier> inner_;
    std::coroutine_handle<> handle_;
};


/**
 * @brief Add timeout semantics to one-shot operations without changing call style.
 *
 * Uses `IOSQE_IO_LINK` to atomically chain the operation SQE with a
 * `io_uring_prep_link_timeout` SQE. The `single_shot_only_operation` constraint
 * provides the `prepare` / `set_result` / `await_resume` interface required by
 * `TimeoutAwaiter`.
 *
 * @tparam Operation One-shot operation type satisfying `single_shot_only_operation`.
 * @tparam Duration  Duration type satisfying `chrono_duration`.
 * @param operation  Operation to wrap with timeout behavior.
 * @param dur        Timeout duration.
 */
template<single_shot_operation Operation, chrono_duration Duration>
auto timeout(Operation&& operation, Duration dur)
{
    return TimeoutAwaiter<std::decay_t<Operation>>{ std::forward<Operation>(operation), dur };
}

/**
 * @brief Add timeout semantics to cancelable operations.
 *
 * Races the inner operation against a `SleepAwaiter` of the given duration
 * using `when_any`. Returns `std::errc::timed_out` if the sleep wins;
 * otherwise forwards the inner operation's own `std::expected` result.
 * The loser is cancelled and both CQEs are consumed before resuming.
 *
 * @tparam Operation Cancelable operation type satisfying `cancelable_operation`.
 * @tparam Duration  Duration type satisfying `chrono_duration`.
 * @param operation  Operation to wrap with timeout behavior.
 * @param dur        Maximum allowed duration before cancellation.
 *
 * @code{.cpp}
 * auto result = co_await timeout(net::receive_all(ctx, sock, buf), 5s);
 * if (!result && result.error() == std::errc::timed_out) { ... }
 * @endcode
 */
template<cancelable_operation Operation, chrono_duration Duration>
    requires (!single_shot_operation<Operation>)
auto timeout(Operation&& operation, Duration dur)
{
    auto& ctx = operation.context();
    return TimeoutWrapper<std::decay_t<Operation>>{ std::forward<Operation>(operation), TimerAwaier{ ctx, dur } };
}

} // namespace async

#endif // BLOG_ASYNC_TIMEOUT_H