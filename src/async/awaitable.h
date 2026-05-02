#ifndef BLOG_ASYNC_AWAITABLE_H
#define BLOG_ASYNC_AWAITABLE_H

#include <concepts>
#include <coroutine>
#include <utility>

namespace async {

/**
 * @brief Constrain types that implement the three co_await protocol methods.
 *
 * This is the core of the C++ coroutine protocol. Types satisfying `awaiter`
 * can be directly `co_await`-ed without any further adaptation.
 */
template<typename T>
concept awaiter = requires(T& t, std::coroutine_handle<> handle)
{
    { t.await_ready() } -> std::convertible_to<bool>;
    t.await_suspend(handle);
    t.await_resume();
};

/**
 * @brief Constrain types that produce an awaiter via member `operator co_await`.
 */
template<typename T>
concept has_operator_co_await = requires(T&& t)
{
    { std::forward<T>(t).operator co_await() } -> awaiter;
};

/**
 * @brief Constrain types that produce an awaiter via free `operator co_await`.
 */
template<typename T>
concept has_global_operator_co_await = requires(T&& t)
{
    { operator co_await(std::forward<T>(t)) } -> awaiter;
};

/**
 * @brief Constrain types that can appear after `co_await`.
 *
 * A type is awaitable if it is directly an `awaiter`, or if it provides
 * an adapter via member or free `operator co_await`.
 */
template<typename T>
concept awaitable = awaiter<T> 
                 || has_operator_co_await<T> 
                 || has_global_operator_co_await<T>;

/**
 * @brief Deduce the result type produced when `co_await`-ing type `T`.
 *
 * Useful in generic coroutine wrappers that need to forward the inner
 * result type without explicitly spelling it out.
 */
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

} // namespace async

#endif // BLOG_ASYNC_AWAITABLE_H