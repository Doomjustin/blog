#ifndef BLOG_ASYNC_TIMEOUT_AWAITER_H
#define BLOG_ASYNC_TIMEOUT_AWAITER_H

#include <cerrno>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <expected>
#include <utility>

#include <liburing.h>

#include <common.h>
#include <operation.h>

namespace async {

/**
 * @brief Constrain inner operations that can be wrapped with timeout semantics.
 *
 * The operation must expose a `context()`, `prepare()`, `set_result()`, and
 * `await_resume()` interface and derive from `Operation` so `TimeoutAwaiter`
 * can set it as CQE user-data and dispatch completions correctly.
 */
template<typename T>
concept single_shot_only_operation = requires (T& op, ::io_uring_sqe* sqe, std::coroutine_handle<> handle)
{
    typename T::resume_type;

    requires std::is_lvalue_reference_v<decltype(op.context())>;
    op.context();
    op.prepare(sqe);
    op.set_result(0, 0);
    { op.await_resume() } -> std::same_as<std::expected<typename T::resume_type, std::error_code>>;
};


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
template<single_shot_only_operation InnerOperation>
class TimeoutAwaiter: public Operation {
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

        timeout_.tv_sec = duration_cast<seconds>(timeout).count();
        timeout_.tv_nsec = duration_cast<nanoseconds>(timeout % 1s).count();
    }

    ~TimeoutAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;

        auto* io_sqe = context().sqe();
        auto* timeout_sqe = context().sqe();

        inner_operation_.prepare(io_sqe);
        io_sqe->flags |= IOSQE_IO_LINK;
        ::io_uring_sqe_set_data(io_sqe, this);

        ::io_uring_prep_link_timeout(timeout_sqe, &timeout_, 0);
        ::io_uring_sqe_set_data(timeout_sqe, this);

        context().track(this);
    }

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (is_timed_out_)
            return unexpected_system_error(std::errc::timed_out);

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
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }
    
    auto context() noexcept -> decltype(std::declval<InnerOperation&>().context())
    {
        return inner_operation_.context();
    }

private:
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
 * @brief Constrain operations that support mid-flight cancellation via a parent combinator.
 *
 * A cancelable operation must:
 * - Expose a `resume_type` result alias.
 * - Return an lvalue reference from `context()`.
 * - Accept `await_suspend(handle)` so `TimeoutCombinator` can call it.
 * - Derive from `CancelableOperation` so the `parent` pointer mechanism is available.
 */
template<typename T>
concept cancelable_operation = requires(T& t)
{
    typename T::resume_type;

    requires std::is_lvalue_reference_v<decltype(t.context())>;
    t.await_suspend(std::coroutine_handle<>());
};


/**
 * @brief Wrap a `CancelableOperation` with an independent timer and mutual cancellation.
 *
 * Unlike `TimeoutAwaiter` (which uses `IOSQE_IO_LINK`), `TimeoutCombinator` submits
 * the inner operation and a separate `io_uring_prep_timeout` SQE independently, then
 * cancels whichever side loses the race:
 * - If the timer fires first (`on_timer_completed`), the inner operation is cancelled
 *   and `await_resume` returns `std::errc::timed_out`.
 * - If the inner operation completes first (`complete`), the timer SQE is cancelled.
 *
 * Both CQEs must arrive before the coroutine is resumed (`pending_cqes_` starts at 2).
 * Cancellation is issued via `IOContext::cancel()`, which does not increment the
 * outstanding-work counter.
 *
 * @tparam Awaiter Cancelable awaiter type satisfying `cancelable_operation`.
 */
template<cancelable_operation Awaiter>
class TimeoutCombinator: public CancelableOperation {
public:
    using resume_type = typename Awaiter::resume_type;

    /**
     * @brief Construct from an inner awaiter and a timeout duration.
     *
     * Sets `awaiter_.parent = this` so that when the inner awaiter calls back
     * into its parent, `TimeoutCombinator` can cancel the pending timer.
     *
     * @tparam Duration `std::chrono::duration` specialization.
     * @param awaiter Inner cancelable awaiter; moved into this combinator.
     * @param timeout Maximum allowed duration for the inner operation.
     */
    template<chrono_duration Duration>
    TimeoutCombinator(Awaiter&& awaiter, Duration timeout)
      : awaiter_{ std::forward<Awaiter>(awaiter) }
    {
        using namespace std::chrono;

        timeout_.tv_sec = duration_cast<seconds>(timeout).count();
        timeout_.tv_nsec = duration_cast<nanoseconds>(timeout % 1s).count();

        awaiter_.parent = this;
    }

    ~TimeoutCombinator() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;

        auto* sqe = context().sqe();
        ::io_uring_prep_timeout(sqe, &timeout_, 0, 0);
        ::io_uring_sqe_set_data(sqe, &timer_);

        context().track(&timer_);
        awaiter_.await_suspend(handle);
    }

    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (state_ == State::TimerCompleted)
            return unexpected_system_error(std::errc::timed_out);

        return awaiter_.await_resume();
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        // 如果内层操作先完成了，取消定时器SQE以避免不必要的超时事件
        if (state_ == State::Pending) {
            state_ = State::AwaiterCompleted;
            context().cancel(&timer_);
        }

        // 等到两个CQE都完成后才返回结果，避免丢失任何一个的完成事件
        if (--pending_cqes_ == 0) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

    auto context() noexcept -> decltype(std::declval<Awaiter&>().context())
    {
        return awaiter_.context();
    }

private:
    enum State: std::uint8_t {
        Pending,
        AwaiterCompleted,
        TimerCompleted
    };

    struct Timer: public Operation {
        TimeoutCombinator* owner;

        Timer(TimeoutCombinator* owner) 
          : owner{ owner } 
        {}

        ~Timer() = default;

        void complete(int result, [[maybe_unused]] std::uint32_t flags) noexcept override
        {
            owner->context().untrack(this);
            owner->on_timer_completed(result);
        }
    };

    Awaiter awaiter_;
    struct __kernel_timespec timeout_;

    std::coroutine_handle<> handle_;
    State state_{ State::Pending };
    int pending_cqes_{ 2 };
    Timer timer_{ this };

    void on_timer_completed(int result) noexcept
    {
        // 如果定时器先完成了，取消内层操作以避免不必要的处理
        if (state_ == State::Pending) {
            state_ = State::TimerCompleted;
            context().cancel(&awaiter_);
        }

        if (--pending_cqes_ == 0) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }
};

} // namespace async

#endif // BLOG_ASYNC_TIMEOUT_AWAITER_H    