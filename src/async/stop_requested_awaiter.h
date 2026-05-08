#ifndef BLOG_ASYNC_STOP_REQUESTED_AWAITER_H
#define BLOG_ASYNC_STOP_REQUESTED_AWAITER_H

#include <atomic>
#include <coroutine>
#include <functional>
#include <memory>
#include <stop_token>
#include <utility>

#include <async/io_context.h>
#include <async/post.h>
#include <async/this_coroutine.h>

namespace async {

class StopRequestedAwaiter {
    using callback_type = std::stop_callback<std::function<void()>>;

    struct State {
        std::atomic<bool> alive{ true };
        std::coroutine_handle<> handle;
    };

    IOContext* context_;
    std::stop_token token_;
    std::shared_ptr<State> state_{ std::make_shared<State>() };
    std::unique_ptr<callback_type> callback_;

public:
    explicit StopRequestedAwaiter(std::stop_token token)
      : StopRequestedAwaiter(this_coroutine::context(), std::move(token))
    {}

    StopRequestedAwaiter(IOContext& context, std::stop_token token)
      : context_(&context), token_(std::move(token))
    {}

    ~StopRequestedAwaiter()
    {
        state_->alive.store(false, std::memory_order_release);
        callback_.reset();
    }

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool { return token_.stop_requested(); }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        state_->handle = handle;
        auto state = state_;
        auto* context = context_;

        callback_ = std::make_unique<callback_type>(
            token_, [state, context] {
                // Bounce the resume onto the event loop to avoid synchronous
                // stop_callback execution resuming the coroutine twice.
                post(*context, [state] {
                    if (state->alive.load(std::memory_order_acquire)) {
                        auto h = std::exchange(state->handle, std::coroutine_handle<>{});
                        if (h)
                            h.resume();
                    }
                });
            });
        return true;
    }

    void await_resume() noexcept
    {
        callback_.reset();
        state_->handle = {};
    }
};

} // namespace async

#endif // BLOG_ASYNC_STOP_REQUESTED_AWAITER_H
