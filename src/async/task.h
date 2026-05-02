#ifndef BLOG_ASYNC_TASK_H
#define BLOG_ASYNC_TASK_H

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

#include <final_awaiter.h>

namespace async {

/**
 * @brief Lazy, joinable coroutine task.
 *
 * `Task<T>` represents a suspended coroutine that produces a value of
 * type `T` (or nothing for the `void` specialization). The coroutine
 * does not start automatically; it is driven by a `co_await` expression
 * in a parent coroutine or by manually resuming the underlying handle.
 *
 * Ownership is move-only: the destructor destroys the coroutine frame
 * if it has not yet been transferred. When `co_await`-ed, symmetric
 * transfer via `FinalAwaiter` resumes the parent without a recursive
 * call stack.
 *
 * @tparam T Return value type; defaults to `void`.
 */
template<typename T = void>
class Task;

template<typename T>
class Task {
public:
    class promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    /**
     * @brief Coroutine promise that stores the return value or exception.
     *
     * The coroutine suspends at both `initial_suspend` and `final_suspend`.
     * The `next` handle is set by `Awaiter::await_suspend` so `FinalAwaiter`
     * can perform the symmetric transfer back to the waiting parent.
     */
    class promise_type {
    public:
        auto get_return_object() noexcept -> Task { return Task{ handle_type::from_promise(*this) }; }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }

        auto final_suspend() noexcept -> FinalAwaiter { return {}; }

        void unhandled_exception() noexcept { exception_ = std::current_exception(); }

        /**
         * @brief Store the coroutine's return value.
         *
         * Accepts any type convertible to `T` so callers can `co_return`
         * without an explicit cast.
         */
        template<typename U>
            requires std::convertible_to<U&&, T>
        void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)
        {
            value_.emplace(std::forward<U>(value));
        }

        /**
         * @brief Extract the stored value or rethrow a captured exception.
         *
         * Called by `Awaiter::await_resume` to deliver the result to the
         * parent coroutine. Clears the stored value after extraction so
         * repeated calls on the same promise are rejected.
         *
         * @return Stored value of type `T`.
         * @throws Whatever the coroutine threw via `unhandled_exception`.
         * @throws std::logic_error If called before a value was set.
         */
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

    /** @brief Take ownership of an existing coroutine handle. */
    Task(handle_type handle)
      : handle_{ handle }
    {}

    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    /** @brief Transfer handle ownership; source becomes empty. */
    Task(Task&& other) noexcept
      : handle_{ std::exchange(other.handle_, {}) }
    {}

    /**
     * @brief Destroy current frame (if any) then take ownership from source.
     *
     * Self-assignment is a no-op.
     */
    auto operator=(Task&& other) noexcept -> Task&
    {
        if (this == &other)
            return *this;

        if (handle_)
            handle_.destroy();

        handle_ = std::exchange(other.handle_, nullptr);
        return *this;
    }

    /** @brief Destroy the coroutine frame if still owned. */
    ~Task()
    {
        if (handle_)
            handle_.destroy();
    }

    /** @brief Return `true` when the coroutine has finished or is empty. */
    [[nodiscard]]
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    /** @brief Expose the underlying coroutine handle for advanced use cases. */
    [[nodiscard]]
    auto handle() const noexcept -> handle_type
    {
        return handle_;
    }

    /**
     * @brief Awaiter that starts this task and resumes the parent on completion.
     *
     * `await_suspend` chains this task to its parent via symmetric transfer.
     * `await_resume` extracts the result (or rethrows a stored exception)
     * from the promise.
     */
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

    /**
     * @brief Produce an `Awaiter` when this task is `co_await`-ed.
     *
     * Rvalue-qualified so the handle is consumed exactly once, preventing
     * accidental double-await of the same task.
     */
    auto operator co_await() && noexcept { return Awaiter{ handle_ }; }

private:
    handle_type handle_{ nullptr };
};


/**
 * @brief `Task<void>` specialization for coroutines that return no value.
 *
 * Identical semantics to `Task<T>` but the promise stores only an
 * exception pointer and `result()` either rethrows or returns normally.
 */
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

        /**
         * @brief Rethrow any exception captured during coroutine execution.
         *
         * Called by `Awaiter::await_resume`; returns normally on success.
         *
         * @throws Whatever the coroutine threw via `unhandled_exception`.
         */
        void result()
        {
            if (exception_)
                std::rethrow_exception(exception_);
        }

    private:
        std::exception_ptr exception_;
    };

    Task() = default;

    /** @brief Take ownership of an existing coroutine handle. */
    Task(handle_type handle)
      : handle_{ handle }
    {}

    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    /** @brief Transfer handle ownership; source becomes empty. */
    Task(Task&& other) noexcept
      : handle_{ std::exchange(other.handle_, nullptr) }
    {}

    /**
     * @brief Destroy current frame (if any) then take ownership from source.
     *
     * Self-assignment is a no-op.
     */
    auto operator=(Task&& other) noexcept -> Task&
    {
        if (this == &other)
            return *this;

        if (handle_)
            handle_.destroy();

        handle_ = std::exchange(other.handle_, {});
        return *this;
    }

    /** @brief Destroy the coroutine frame if still owned. */
    ~Task()
    {
        if (handle_)
            handle_.destroy();
    }

    /** @brief Return `true` when the coroutine has finished or is empty. */
    [[nodiscard]]
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    /** @brief Expose the underlying coroutine handle for advanced use cases. */
    [[nodiscard]]
    auto handle() const noexcept -> handle_type
    {
        return handle_;
    }

    /**
     * @brief Awaiter that starts this task and resumes the parent on completion.
     *
     * `await_resume` calls `promise_type::result()` which rethrows any stored
     * exception; it returns normally on success.
     */
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

        void await_resume() const
        {
            if (!handle_)
                throw std::logic_error{ "Invalid coroutine handle" };

            handle_.promise().result();
        }

    private:
        handle_type handle_;
    };

    /**
     * @brief Produce an `Awaiter` when this task is `co_await`-ed.
     *
     * Rvalue-qualified so the handle is consumed exactly once, preventing
     * accidental double-await of the same task.
     */
    auto operator co_await() && noexcept { return Awaiter{ handle_ }; }

private:
    handle_type handle_{ nullptr };
};

} // namespace async

#endif // BLOG_ASYNC_TASK_H