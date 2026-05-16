#ifndef BLOG_ASYNC_TASK_H
#define BLOG_ASYNC_TASK_H

#include <coroutine>
#include <exception>
#include <optional>
#include <stop_token>
#include <utility>

#include "async/operation.h"
#include "async/stop_then.h"

#include <async/final_awaiter.h>

namespace async {

template<typename T>
struct is_stop_token_wrapped: std::false_type {};

template<typename T>
struct is_stop_token_wrapped<StopTokenWrapper<T>>: std::true_type {};

template<typename T>
concept already_wrapped = is_stop_token_wrapped<std::remove_cvref_t<T>>::value;

template<typename T>
class TaskReturnType {
protected:
    std::optional<T> result_;

public:
    template<typename U>
        requires std::convertible_to<U&&, T>
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
    {
        result_.emplace(std::forward<U>(value));
    }

    auto result() noexcept -> T&&
    {
        return std::move(*result_);
    }
};

template<>
class TaskReturnType<void> {
public:
    void return_void() noexcept {}
};


template<typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type: TaskReturnType<T> {
        std::coroutine_handle<> next{ nullptr };
        union { std::exception_ptr exception; };
        bool has_exception{ false };
        std::stop_token stop_token;

        promise_type() {}

        ~promise_type()
        {
            if (has_exception)
                exception.~exception_ptr();
        }

        auto get_return_object() noexcept -> Task 
        { 
            return Task{ handle_type::from_promise(*this) }; 
        }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }

        auto final_suspend() noexcept -> FinalAwaiter { return {}; }

        void unhandled_exception() noexcept 
        { 
            new (&exception) std::exception_ptr(std::current_exception());
            has_exception = true;
        }

        template<cancelable_operation Operation>
            requires (!already_wrapped<Operation>)
        auto await_transform(Operation&& operation)
        {
            return stop_then(std::forward<Operation>(operation), stop_token);
        }

        template<typename Awaitable>
        auto await_transform(Awaitable&& awaitable) -> decltype(auto)
        {
            return std::forward<Awaitable>(awaitable);
        }
    };

    Task() = default;

    Task(handle_type handle)
      : handle_{ handle }
    {}

    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    Task(Task&& other) noexcept
      : handle_{ std::exchange(other.handle_, {}) }
    {}

    auto operator=(Task&& other) noexcept -> Task&
    {
        if (this == &other)
            return *this;

        if (handle_)
            handle_.destroy();

        handle_ = std::exchange(other.handle_, {});
        return *this;
    }

    ~Task()
    {
        if (handle_)
            handle_.destroy();
    }

    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> parent) -> std::coroutine_handle<>
    {
        handle_.promise().next = parent;

        // 如果父协程中有 stop_token，则将其传递给子协程
        if constexpr (requires { parent.promise().stop_token; })
            handle_.promise().stop_token = parent.promise().stop_token;

        return handle_;
    }

    auto await_resume() const
    {
        if (handle_.promise().has_exception)
            std::rethrow_exception(handle_.promise().exception);

        if constexpr (!std::is_void_v<T>)
            return handle_.promise().result();
        else
            return;
    }

    auto handle() const noexcept
    {
        return handle_;
    }

    void release() noexcept
    {
        handle_ = nullptr;
    }

private:
    handle_type handle_;
};

} // namespace async

#endif // BLOG_ASYNC_TASK_H