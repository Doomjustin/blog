#ifndef BLOG_ASYNC_RETRY_H
#define BLOG_ASYNC_RETRY_H

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>

#include <exceptions.h>
#include <operation.h>
#include <sleep_for.h>
#include <timeout_awaiter.h>

namespace async {

/**
 * @brief Awaiter that retries a cancelable operation on failure.
 *
 * Submits the inner operation via `factory()` on each attempt. On failure,
 * enters a delay phase using `TimerAwaier` before the next attempt. The
 * delay duration is computed by `delay_strategy(attempt_index)` where
 * `attempt_index` starts at 1 after the first failure.
 *
 * Both the IO and delay phases set `parent = this`, so external cancellation
 * (e.g. from `when_any` or `timeout`) routes correctly through `complete()`.
 *
 * @tparam Factory       Nullary callable returning a `cancelable_operation`.
 * @tparam DelayStrategy Callable `(std::size_t) -> chrono_duration`.
 *
 * ## Example
 *
 * ```cpp
 * // Fixed 100 ms delay, up to 3 attempts
 * auto result = co_await RetryCombinator{
 *     3, [&]{ return net::send(ctx, sock, buf); },
 *     [](std::size_t) { return std::chrono::milliseconds{100}; }
 * };
 *
 * // Exponential backoff: 100 ms, 200 ms, 400 ms
 * auto result = co_await RetryCombinator{
 *     4, [&]{ return net::send(ctx, sock, buf); },
 *     [](std::size_t n) { return std::chrono::milliseconds{100} * (1 << n); }
 * };
 * ```
 */
template<typename Factory, typename DelayStrategy>
    requires cancelable_operation<std::invoke_result_t<Factory>>
class RetryCombinator: public CancelableOperation {
public:
    using inner_awaiter_type = std::invoke_result_t<Factory>;
    using resume_type        = typename inner_awaiter_type::resume_type;
    using context_type       = IOContext;

    RetryCombinator(std::size_t max_retries, Factory factory, DelayStrategy delay_strategy)
      : max_retries_{ max_retries }
      , retries_left_{ max_retries }
      , factory_{ std::move(factory) }
      , delay_strategy_{ std::move(delay_strategy) }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;
        start_io_attempt();
    }

    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (is_canceling_)
            return unexpected_system_error(std::errc::operation_canceled);
        return io_awaiter_->await_resume();
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        // External cancel (e.g. timeout): propagate immediately.
        if (is_canceling_) {
            this->resume(handle_, result, flags);
            return;
        }

        if (state_ == State::Delaying) {
            // Timer fired: start the next IO attempt.
            start_io_attempt();
            return;
        }

        // IO completed: success or retries exhausted → done.
        if (result >= 0 || retries_left_ == 0) {
            this->resume(handle_, result, flags);
        } else {
            --retries_left_;
            start_delay();
        }
    }

    auto context() noexcept -> context_type&
    {
        if (io_awaiter_)
            return io_awaiter_->context();
        return this_coroutine::context();
    }

private:
    enum class State : std::uint8_t { IoRunning, Delaying };

    void start_io_attempt() noexcept
    {
        state_ = State::IoRunning;
        io_awaiter_.emplace(factory_());
        io_awaiter_->parent = this;
        io_awaiter_->await_suspend(handle_);
    }

    void start_delay() noexcept
    {
        state_ = State::Delaying;
        auto dur = delay_strategy_(max_retries_ - retries_left_);
        delay_awaiter_.emplace(context(), dur);
        delay_awaiter_->parent = this;
        delay_awaiter_->await_suspend(handle_);
    }

    std::size_t max_retries_;
    std::size_t retries_left_;
    Factory       factory_;
    DelayStrategy delay_strategy_;

    std::coroutine_handle<> handle_{ nullptr };
    State state_{ State::IoRunning };

    std::optional<inner_awaiter_type> io_awaiter_;
    std::optional<TimerAwaier>        delay_awaiter_;
};

/**
 * @brief Deduction guide: deduces `Factory` and `DelayStrategy` from arguments.
 */
template<typename Factory, typename DelayStrategy>
RetryCombinator(std::size_t, Factory, DelayStrategy) -> RetryCombinator<Factory, DelayStrategy>;


// ---------------------------------------------------------------------------
// Built-in delay strategies
// ---------------------------------------------------------------------------

namespace retry_strategy {

/**
 * @brief Return the same duration on every attempt.
 *
 * @code{.cpp}
 * co_await retry(3, retry_strategy::fixed(100ms), [&]{ return send(...); });
 * @endcode
 */
template<chrono_duration Delay>
auto fixed(Delay d)
{
    return [d](std::size_t /*attempt*/) { return d; };
}

/**
 * @brief Multiply the initial delay by `multiplier` after each failure.
 *
 * attempt 0 → initial_delay * multiplier^0
 * attempt 1 → initial_delay * multiplier^1
 * ...
 *
 * @code{.cpp}
 * co_await retry(4, retry_strategy::exponential(100ms, 2.0f), [&]{ return send(...); });
 * @endcode
 */
template<chrono_duration Delay>
auto exponential(Delay initial_delay, float multiplier)
{
    return [initial_delay, multiplier](std::size_t attempt) {
        std::chrono::duration<double, typename Delay::period> d{ initial_delay };
        for (std::size_t i = 0; i < attempt; ++i)
            d *= multiplier;
        return std::chrono::round<Delay>(d);
    };
}

} // namespace retry_strategy


// ---------------------------------------------------------------------------
// retry() helpers
// ---------------------------------------------------------------------------

/**
 * @brief Retry with a user-supplied delay strategy.
 *
 * @param max_retries   Total number of attempts.
 * @param delay_strategy Callable `(std::size_t attempt) -> chrono_duration`.
 * @param factory        Nullary callable returning a `cancelable_operation`.
 *
 * @code{.cpp}
 * co_await retry(3, retry_strategy::fixed(50ms), [&]{ return send(...); });
 * co_await retry(4, retry_strategy::exponential(100ms, 2.0f), [&]{ return send(...); });
 * // custom strategy:
 * co_await retry(5, [](std::size_t n) { return 10ms * (n + 1); }, [&]{ return send(...); });
 * @endcode
 */
template<typename DelayStrategy, typename Factory>
    requires cancelable_operation<std::invoke_result_t<Factory>>
auto retry(std::size_t max_retries, DelayStrategy&& delay_strategy, Factory&& factory)
{
    return RetryCombinator{
        max_retries,
        std::forward<Factory>(factory),
        std::forward<DelayStrategy>(delay_strategy)
    };
}

/**
 * @brief Convenience overload: fixed delay between attempts.
 *
 * @code{.cpp}
 * co_await retry(3, 100ms, [&]{ return send(...); });
 * @endcode
 */
template<chrono_duration Delay, typename Factory>
    requires cancelable_operation<std::invoke_result_t<Factory>>
auto retry(std::size_t max_retries, Delay delay, Factory&& factory)
{
    return retry(max_retries, retry_strategy::fixed(delay), std::forward<Factory>(factory));
}

} // namespace async

#endif // BLOG_ASYNC_RETRY_H
