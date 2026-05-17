#ifndef BLOG_ASYNC_TIMEOUT_OPERATION_H
#define BLOG_ASYNC_TIMEOUT_OPERATION_H

#include "operation.h"
#include "timeout_awaiter.h"
#include "timeout_wrapper.h"

namespace async {

/// @brief 统一的超时包装工厂。
/// @tparam Awaitable 被包装的 awaitable 类型。
/// @tparam Duration 满足 `chrono_duration` concept 的时长类型。
/// @param[in] awaitable 被包装对象。
/// @param[in] timeout 超时时长。
/// @return 一个可 `co_await` 的 timeout 包装对象。
template<typename Awaitable, chrono_duration Duration>
auto timeout(Awaitable&& awaitable, Duration timeout) -> decltype(auto)
{
    if constexpr (std::derived_from<Awaitable, Operation>)
        return TimeoutAwaiter<Awaitable>{ std::forward<Awaitable>(awaitable), timeout };
    else
        return TimeoutWrapper<Awaitable, Duration>{ std::forward<Awaitable>(awaitable), timeout };
}

} // namespace async

#endif // BLOG_ASYNC_TIMEOUT_OPERATION_H