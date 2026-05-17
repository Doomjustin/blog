#ifndef BLOG_ASYNC_TASK_GROUP_H
#define BLOG_ASYNC_TASK_GROUP_H

#include <atomic>
#include <mutex>
#include <stop_token>
#include <system_error>
#include <variant>

#include "channel.h"
#include "co_spawn.h"
#include "task.h"
#include "this_coro.h"

namespace async {

/// @brief 结构化并发任务组，负责 stop 传播与收敛等待。
///
/// @note 使用约束：调用方必须在 `spawn` 后主动 `co_await join()` 收敛所有子任务。
/// 若未调用 `join()`，本类型不提供任何生命周期与异常传播保证。
class TaskGroup {
private:
    struct State {
        std::atomic<std::size_t> pending_{ 1 };
        std::atomic<bool> closed_{ false };

        std::stop_source stop_source_{};
        Channel<std::monostate> drained_{ 1 };

        std::mutex mutex_;
        std::exception_ptr first_exception_{};

        void record_exception(std::exception_ptr exception)
        {
            std::scoped_lock lock{ mutex_ };
            if (!first_exception_)
                first_exception_ = std::move(exception);
        }

        auto take_exception() -> std::exception_ptr
        {
            std::scoped_lock lock{ mutex_ };
            return first_exception_;
        }
    };

    State state_{};

    static auto run_child(State* state, Task<> task) -> Task<>
    {
        try {
            co_await std::move(task);
        }
        catch (...) {
            state->record_exception(std::current_exception());
        }

        if (state->pending_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            state->drained_.try_send(std::monostate{});
    }

public:
    TaskGroup() = default;

    TaskGroup(const TaskGroup&) = delete;
    auto operator=(const TaskGroup&) -> TaskGroup& = delete;

    TaskGroup(TaskGroup&&) noexcept = delete;
    auto operator=(TaskGroup&&) noexcept -> TaskGroup& = delete;

    ~TaskGroup() = default;

    /// @brief 读取任务组 stop_token。
    /// @return 可传给子任务的 stop_token。
    auto stop_token() const noexcept -> std::stop_token
    {
        return state_.stop_source_.get_token();
    }

    /// @brief 广播 stop 请求给任务组中的所有子任务。
    void request_stop() const noexcept
    {
        state_.stop_source_.request_stop();
    }

    /// @brief 启动一个子任务（detached），并纳入任务组收敛管理。
    /// @param[in] task 待纳入任务组管理的 Task。
    auto spawn(Task<> task) -> Task<>
    {
        if (state_.closed_.load(std::memory_order_acquire))
            co_return;

        state_.pending_.fetch_add(1, std::memory_order_acq_rel);
        auto& context = co_await this_coro::context;
        co_spawn(context, state_.stop_source_.get_token(), run_child(&state_, std::move(task)));
    }

    /// @brief 等待任务组内所有子任务完成并排干。
    ///
    /// @note 若省略该调用，则 TaskGroup 的行为不受保证。
    /// @return Task<>。
    auto join() -> Task<>
    {
        state_.closed_.store(true, std::memory_order_release);

        if (state_.pending_.fetch_sub(1, std::memory_order_acq_rel) != 1) {
            auto drained = co_await state_.drained_.async_receive();
            if (!drained)
                throw std::system_error(drained.error());
        }

        if (auto exception = state_.take_exception(); exception)
            std::rethrow_exception(exception);
    }
};

} // namespace async

#endif // BLOG_ASYNC_TASK_GROUP_H
