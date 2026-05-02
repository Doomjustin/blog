#ifndef BLOG_ASYNC_SLEEP_FOR_H
#define BLOG_ASYNC_SLEEP_FOR_H

#include <common.h>
#include <sleep_awaiter.h>

namespace async {

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
auto sleep_for(Duration duration) -> SleepAwaiter
{
    return SleepAwaiter{ duration };
}

} // namespace async

#endif // BLOG_ASYNC_SLEEP_FOR_H