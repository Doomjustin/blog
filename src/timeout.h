#ifndef BLOG_TIMEOUT_H
#define BLOG_TIMEOUT_H

#include "timeout_awaiter.h"

/**
 * @brief Add timeout semantics to one-shot operations without changing call style.
 *
 * The `single_shot_only_operation` constraint ensures the wrapped awaitable
 * has the minimal contract required by `TimeoutAwaiter` (`context/prepare/
 * set_result/await_resume`) and can be safely linked with timeout request
 * completion logic.
 *
 * @tparam Operation One-shot operation type satisfying
 * `single_shot_only_operation`.
 * @tparam Duration Duration type satisfying `chrono_duration`.
 * @param awaitable Operation to wrap with timeout behavior.
 * @param timeout Timeout duration.
 * @return Awaiter that resolves to the inner operation result or timeout error.
 *
 * @code{.cpp}
 * auto result = co_await timeout(socket.async_read_some(buffer), 3s);
 * if (!result) {
 *     // Handle timeout or read error
 * }
 * @endcode
 */
template<single_shot_only_operation Operation, chrono_duration Duration>
auto timeout(Operation&& awaitable, Duration timeout) -> TimeoutAwaiter<std::decay_t<Operation>>
{
    return TimeoutAwaiter<std::decay_t<Operation>>{ std::forward<Operation>(awaitable), timeout };
}

#endif // BLOG_TIMEOUT_H