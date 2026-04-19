#ifndef BLOG_AWAITABLE_H
#define BLOG_AWAITABLE_H

#include <concepts>
#include <coroutine>
#include <utility>

template<typename T>
concept awaiter = requires(T& t, std::coroutine_handle<> handle)
{
    { t.await_ready() } -> std::convertible_to<bool>;
    t.await_suspend(handle);
    t.await_resume();
};

template<typename T>
concept has_operator_co_await = requires(T&& t)
{
    { std::forward<T>(t).operator co_await() } -> awaiter;
};

template<typename T>
concept has_global_operator_co_await = requires(T&& t)
{
    { operator co_await(std::forward<T>(t)) } -> awaiter;
};

template<typename T>
concept awaitable = awaiter<T> 
                 || has_operator_co_await<T> 
                 || has_global_operator_co_await<T>;

template<typename T>
using await_result_t = decltype(
    []() 
    {
        if constexpr (has_operator_co_await<T>) {
            return std::declval<T>().operator co_await().await_resume();
        } else if constexpr (has_global_operator_co_await<T>) {
            return operator co_await(std::declval<T>()).await_resume();
        } else {
            return std::declval<T>().await_resume();
        }
    }()
);

#endif // BLOG_AWAITABLE_H