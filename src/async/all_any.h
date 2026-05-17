#ifndef BLOG_ASYNC_ALL_ANY_H
#define BLOG_ASYNC_ALL_ANY_H

#include <utility>

#include "task.h"
#include "task_group.h"

namespace async {

namespace detail {

inline auto run_win_first(TaskGroup& group, Task<> task) -> Task<>
{
    try {
        co_await std::move(task);
        group.request_stop();
    }
    catch (...) {
        group.request_stop();
        throw;
    }
}

} // namespace detail

/// @brief 并发启动全部任务并等待全部收敛完成。
/// @tparam Tasks `Task<>` 参数包。
/// @param[in] tasks 待并发执行的任务。
/// @return Task<>。
template<typename... Tasks>
    requires(std::same_as<std::remove_cvref_t<Tasks>, Task<>> && ...)
auto all(Tasks&&... tasks) -> Task<>
{
    TaskGroup group;

    (co_await group.spawn(std::forward<Tasks>(tasks)), ...);

    co_await group.join();
}

/// @brief Win-first：首个任务完成后请求停止其余任务，并等待全部收敛。
/// @tparam Tasks `Task<>` 参数包。
/// @param[in] tasks 待并发竞速的任务。
/// @return Task<>。
template<typename... Tasks>
    requires(std::same_as<std::remove_cvref_t<Tasks>, Task<>> && ...)
auto any(Tasks&&... tasks) -> Task<>
{
    TaskGroup group;

    (co_await group.spawn(detail::run_win_first(group, std::forward<Tasks>(tasks))), ...);

    co_await group.join();
}

} // namespace async

#endif // BLOG_ASYNC_ALL_ANY_H
