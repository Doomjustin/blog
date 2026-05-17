#ifndef BLOG_ASYNC_SLEEP_H
#define BLOG_ASYNC_SLEEP_H

#include <common/common.h>

#include "timer_awaiter.h"

namespace async {

template<chrono_duration Duration>
auto sleep_for(Duration duration) -> TimerAwaiter
{
    return TimerAwaiter{ duration };
}

template<typename Clock, typename Duration>
auto sleep_until(std::chrono::time_point<Clock, Duration> timepoint) -> TimerAwaiter
{
    return TimerAwaiter{ timepoint };
}

} // namespace async

#endif // BLOG_ASYNC_SLEEP_H