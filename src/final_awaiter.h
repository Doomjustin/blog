#ifndef BLOG_FINAL_AWAITER_H
#define BLOG_FINAL_AWAITER_H

#include <coroutine>

class FinalAwaiter {
public:
    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) const noexcept 
        -> std::coroutine_handle<>
    {
        auto next = handle.promise().next;
        return next ? next : std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

#endif // BLOG_FINAL_AWAITER_H