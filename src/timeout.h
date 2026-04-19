#ifndef BLOG_TIMEOUT_H
#define BLOG_TIMEOUT_H

#include "timeout_awaiter.h"

template<uring_operation Operation, chrono_duration Duration>
auto timeout(Operation&& awaitable, Duration timeout) -> TimeoutAwaiter<std::decay_t<Operation>>
{
    return TimeoutAwaiter<std::decay_t<Operation>>{ std::forward<Operation>(awaitable), timeout };
}

#endif // BLOG_TIMEOUT_H