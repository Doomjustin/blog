#ifndef BLOG_TASK_H
#define BLOG_TASK_H

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

#include "final_awaiter.h"


template<typename T = void>
class Task;

template<typename T>
class Task {
public:
    class promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    class promise_type {
    public:
        auto get_return_object() noexcept -> Task { return Task{ handle_type::from_promise(*this) }; }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }

        auto final_suspend() noexcept -> FinalAwaiter { return {}; }

        void unhandled_exception() noexcept { exception_ = std::current_exception(); }

        template<typename U>
            requires std::convertible_to<U&&, T>
        void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        {
            value_.emplace(std::forward<U>(value));
        }

        [[nodiscard]]
        auto result() -> T
        {
            if (exception_)
                std::rethrow_exception(exception_);

            if (!value_)
                throw std::logic_error{ "No value returned from coroutine" };

            auto out = std::move(*value_);
            value_.reset();
            return out;
        }

        std::coroutine_handle<> next{ nullptr };

    private:
        std::exception_ptr exception_;
        std::optional<T> value_;
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

        handle_ = std::exchange(other.handle_, nullptr);
        return *this;
    }

    ~Task()
    {
        if (handle_)
            handle_.destroy();
    }

    [[nodiscard]]
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    [[nodiscard]]
    auto handle() const noexcept -> handle_type
    {
        return handle_;
    }

    class Awaiter {
    public:
        explicit Awaiter(handle_type handle)
          : handle_{ handle }
        {}

        [[nodiscard]]
        auto await_ready() const noexcept -> bool
        {
            return !handle_ || handle_.done();
        }

        auto await_suspend(std::coroutine_handle<> next) -> std::coroutine_handle<>
        {
            handle_.promise().next = next;
            return handle_;
        }

        auto await_resume() const -> T
        {
            if (!handle_)
                throw std::logic_error{ "Invalid coroutine handle" };

            return handle_.promise().result();
        }

    private:
        handle_type handle_;
    };

    auto operator co_await() && noexcept { return Awaiter{ handle_ }; }

private:
    handle_type handle_{ nullptr };
};


template<>
class Task<void> {
public:
    class promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    class promise_type {
    public:
        std::coroutine_handle<> next{ nullptr };

        auto get_return_object() noexcept -> Task { return Task{ handle_type::from_promise(*this) }; }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }

        auto final_suspend() noexcept -> FinalAwaiter { return {}; }

        void unhandled_exception() noexcept { exception_ = std::current_exception(); }

        void return_void() noexcept {}

        void result()
        {
            if (exception_)
                std::rethrow_exception(exception_);
        }

    private:
        std::exception_ptr exception_;
    };

    Task() = default;

    Task(handle_type handle)
      : handle_{ handle }
    {}

    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    Task(Task&& other) noexcept
      : handle_{ std::exchange(other.handle_, nullptr) }
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

    [[nodiscard]]
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    [[nodiscard]]
    auto handle() const noexcept -> handle_type
    {
        return handle_;
    }

    class Awaiter {
    public:
        explicit Awaiter(handle_type handle)
          : handle_{ handle }
        {
        }

        [[nodiscard]]
        auto await_ready() const noexcept -> bool
        {
            return !handle_ || handle_.done();
        }

        auto await_suspend(std::coroutine_handle<> next) -> std::coroutine_handle<>
        {
            handle_.promise().next = next;
            return handle_;
        }

        void await_resume() const
        {
            if (!handle_)
                throw std::logic_error{ "Invalid coroutine handle" };

            handle_.promise().result();
        }

    private:
        handle_type handle_;
    };

    auto operator co_await() && noexcept { return Awaiter{ handle_ }; }

private:
    handle_type handle_{ nullptr };
};

#endif // BLOG_TASK_H