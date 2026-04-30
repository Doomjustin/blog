#ifndef BLOG_ASYNC_FINAL_AWAITER_H
#define BLOG_ASYNC_FINAL_AWAITER_H

#include <coroutine>

namespace async {

/**
 * @brief Resume a waiting parent coroutine when the current one finishes.
 *
 * Used as the `final_suspend` return value in coroutine promise types.
 * By returning the parent handle from `await_suspend`, the C++ runtime
 * performs a symmetric transfer rather than an extra scheduler round trip,
 * keeping the call stack flat and avoiding potential stack overflow under
 * deep coroutine chains.
 */
class FinalAwaiter {
public:
    /**
     * @brief Always suspend so the parent can be notified via symmetric transfer.
     */
    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /**
     * @brief Perform symmetric transfer to the parent coroutine handle.
     *
     * If no parent was registered, resumes the no-op coroutine to allow
     * the runtime to destroy the finished coroutine frame safely.
     *
     * @tparam Promise Promise type that exposes a `next` handle field.
     * @param handle Handle of the completing coroutine.
     * @return Parent handle or `noop_coroutine()`.
     */
    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) const noexcept 
        -> std::coroutine_handle<>
    {
        auto next = handle.promise().next;
        return next ? next : std::noop_coroutine();
    }

    /** @brief No value to return; parent obtains result through its own promise. */
    void await_resume() const noexcept {}
};

} // namespace async

#endif // BLOG_ASYNC_FINAL_AWAITER_H