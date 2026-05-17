#ifndef BLOG_ASYNC_TASK_H
#define BLOG_ASYNC_TASK_H

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

#include "stoppable_promise.h"

namespace async {

/// @brief Task 的非 void 返回值存储策略。
/// @tparam T 任务返回类型。
template<typename T>
class TaskReturnType {
protected:
    std::optional<T> result_;

public:
    /// @brief 保存 coroutine 返回值。
    /// @param[in] value 要写入的返回值。
    template<typename U>
        requires std::convertible_to<U&&, T>
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
    {
        result_.emplace(std::forward<U>(value));
    }

    void return_value(std::in_place_t tag) noexcept(std::is_nothrow_default_constructible_v<T>)
    {
        result_.emplace();
    }

    /// @brief 取出已保存的返回值。
    /// @return 以右值形式返回保存的结果。
    auto result() noexcept -> T&&
    {
        return std::move(*result_);
    }
};

/// @brief Task<void> 的返回值策略特化。
template<>
class TaskReturnType<void> {
public:
    /// @brief 处理 void 返回。
    void return_void() noexcept {}
};

/// @brief 支持 co_await 的可组合任务类型。
///
/// Task 采用 lazy 启动语义：创建后不会立刻执行，只有被 co_await 或
/// 作为 coroutine 返回对象继续驱动时才会运行。
/// @tparam T 任务结果类型，默认为 void。
template<typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

private:
    handle_type handle_;

public:
    /// @brief Task 对应的 coroutine promise 类型。
    struct promise_type
      : TaskReturnType<T>
      , StoppablePromise {

        union {
            std::exception_ptr exception;
        };
        bool has_exception{ false };
        std::coroutine_handle<> next;

        /// @brief 默认构造 promise。
        promise_type() {}

        /// @brief 析构 promise 并释放异常对象。
        ~promise_type()
        {
            if (has_exception)
                exception.~exception_ptr();
        }

        /// @brief 构造 Task 返回对象。
        /// @return 与当前 promise 绑定的 Task。
        auto get_return_object() noexcept -> Task
        {
            return Task{ handle_type::from_promise(*this) };
        }

        /// @brief 初始挂起点，Task 采用 lazy 启动。
        /// @return 总是 suspend_always。
        auto initial_suspend() noexcept -> std::suspend_always
        {
            return {};
        }

        /// @brief 最终挂起点，将控制权返回给父 coroutine。
        /// @return 一个在 await_suspend 中恢复 next 的 awaiter。
        auto final_suspend() noexcept
        {
            struct Awaiter {
                promise_type* promise;

                constexpr auto await_ready() const noexcept -> bool
                {
                    return false;
                }

                auto await_suspend(std::coroutine_handle<> handle) noexcept
                {
                    return promise->next ? promise->next : std::noop_coroutine();
                }

                void await_resume() noexcept {}
            };

            return Awaiter{ this };
        }

        /// @brief 捕获未处理异常。
        void unhandled_exception() noexcept
        {
            new (&exception) std::exception_ptr(std::current_exception());
            has_exception = true;
        }
    };

    Task() = default;

    /// @brief 使用 coroutine handle 构造 Task。
    /// @param[in] handle 与 promise 绑定的句柄。
    Task(handle_type handle)
      : handle_{ handle }
    {}

    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    /// @brief 移动构造，转移 coroutine 所有权。
    /// @param[in] other 源 Task。
    Task(Task&& other) noexcept
      : handle_{ std::exchange(other.handle_, {}) }
    {}

    /// @brief 移动赋值，释放旧句柄并接管新句柄。
    /// @param[in] other 源 Task。
    /// @return 当前对象引用。
    auto operator=(Task&& other) noexcept -> Task&
    {
        if (this == &other)
            return *this;

        if (handle_)
            handle_.destroy();

        handle_ = std::exchange(other.handle_, {});
        return *this;
    }

    /// @brief 析构 Task，销毁仍持有的 coroutine frame。
    ~Task()
    {
        if (handle_)
            handle_.destroy();
    }

    /// @brief Task 作为 awaitable 时总是先挂起，由 await_suspend 驱动。
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /// @brief 挂起父 coroutine，连接 continuation 并传播执行环境。
    /// @param[in] parent 父 coroutine 句柄。
    /// @return 子 Task 对应的 coroutine 句柄。
    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> parent) -> std::coroutine_handle<>
    {
        handle_.promise().next = parent;

        if constexpr (requires { parent.promise().context; })
            handle_.promise().context = parent.promise().context;

        if constexpr (requires { parent.promise().stop_token; })
            handle_.promise().stop_token = parent.promise().stop_token;

        return handle_;
    }

    /// @brief 恢复后返回 Task 结果或重新抛出捕获异常。
    /// @return 当 T 非 void 时返回结果；当 T 为 void 时无返回值。
    auto await_resume() const
    {
        if (handle_.promise().has_exception)
            std::rethrow_exception(handle_.promise().exception);

        if constexpr (!std::is_void_v<T>)
            return handle_.promise().result();
        else
            return;
    }

    /// @brief 获取底层 coroutine handle。
    /// @return 当前持有的句柄。
    auto handle() const noexcept
    {
        return handle_;
    }

    /// @brief 放弃句柄所有权，不销毁 coroutine frame。
    void release() noexcept
    {
        handle_ = nullptr;
    }

    /// @brief 直接恢复 Task 对应的 coroutine。
    auto resume()
    {
        std::exchange(handle_, nullptr).resume();
    }

    /// @brief 直接调用 Task，等价于 resume()。
    auto operator()()
    {
        resume();
    }
};

} // namespace async

#endif // BLOG_ASYNC_TASK_H