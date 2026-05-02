#ifndef BLOG_TIMEOUT_H
#define BLOG_TIMEOUT_H

#include "timeout_awaiter.h"

/**
 * @brief Add timeout semantics to one-shot operations without changing call style.
 *
 * Wraps the operation and a `io_uring_prep_link_timeout` SQE using
 * `IOSQE_IO_LINK` so the timer and the operation are submitted atomically.
 * The `single_shot_only_operation` constraint provides the `prepare` /
 * `set_result` / `await_resume` interface required by `TimeoutAwaiter`.
 *
 * @tparam Operation One-shot operation type satisfying `single_shot_only_operation`.
 * @tparam Duration  Duration type satisfying `chrono_duration`.
 * @param operation  Operation to wrap with timeout behavior.
 * @param timeout    Timeout duration.
 * @return `TimeoutAwaiter` that resolves to the inner operation result or `timed_out` error.
 *
 * @code{.cpp}
 * auto result = co_await timeout(socket.async_read_some(buffer), 3s);
 * if (!result) {
 *     // Handle timeout or read error
 * }
 * @endcode
 */
template<single_shot_only_operation Operation, chrono_duration Duration>
auto timeout(Operation&& operation, Duration timeout) -> TimeoutAwaiter<std::decay_t<Operation>>
{
    return TimeoutAwaiter<std::decay_t<Operation>>{ std::forward<Operation>(operation), timeout };
}

/**
 * @brief Add timeout semantics to cancelable operations without changing call style.
 *
 * This overload handles operations that derive from `CancelableOperation`,
 * which route completions through a parent combinator pointer instead of
 * resuming the coroutine directly. `TimeoutCombinator` issues an independent
 * timer SQE alongside the inner operation; whichever completes first triggers
 * cancellation of the other.
 *
 * @tparam Operation Cancelable operation type satisfying `cancelable_operation`.
 * @tparam Duration  Duration type satisfying `chrono_duration`.
 * @param operation  Operation to wrap with timeout behavior.
 * @param timeout    Maximum allowed duration before cancellation.
 * @return Awaiter that resolves to the inner operation result or `timed_out` error.
 *
 * @code{.cpp}
 * auto result = co_await timeout(write(socket, buf), 5s);
 * if (!result && result.error() == std::errc::timed_out) {
 *     // Handle write timeout
 * }
 * @endcode
 */
template<cancelable_operation Operation, chrono_duration Duration>
auto timeout(Operation&& operation, Duration timeout) -> TimeoutCombinator<std::decay_t<Operation>>
{
    return TimeoutCombinator<std::decay_t<Operation>>{ std::forward<Operation>(operation), timeout };
}

#endif // BLOG_TIMEOUT_H