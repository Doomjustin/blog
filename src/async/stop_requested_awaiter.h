#ifndef BLOG_ASYNC_STOP_REQUESTED_AWAITER_H
#define BLOG_ASYNC_STOP_REQUESTED_AWAITER_H

#include <coroutine>
#include <functional>
#include <memory>
#include <stop_token>

namespace async {

class StopRequestedAwaiter {
    std::stop_token token_;
    std::coroutine_handle<> handle_;
    bool signaled_{ false };
    std::unique_ptr<std::stop_callback<std::function<void()>>> callback_;

public:
    explicit StopRequestedAwaiter(std::stop_token token)
        : token_(std::move(token)), handle_(nullptr)
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool { return token_.stop_requested(); }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        callback_ = std::make_unique<std::stop_callback<std::function<void()>>>(
            token_, [this] {
                signaled_ = true;
                if (handle_)
                    handle_.resume();
            });
        return !signaled_;
    }

    void await_resume() noexcept { callback_.reset(); }
};

} // namespace async

#endif // BLOG_ASYNC_STOP_REQUESTED_AWAITER_H
