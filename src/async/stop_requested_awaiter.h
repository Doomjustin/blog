#ifndef BLOG_ASYNC_STOP_REQUESTED_AWAITER_H
#define BLOG_ASYNC_STOP_REQUESTED_AWAITER_H

#include <coroutine>
#include <optional>
#include <stop_token>
#include <utility>

#include <async/io_context.h>
#include <async/post.h>

namespace async {

class StopRequestedAwaiter {
private:
    struct CancelFn {
        StopRequestedAwaiter* self;

        void operator()() noexcept 
        {
            post(*self->context_, [handle = self->handle_]() { if (handle) handle.resume(); });
        }
    };

    IOContext* context_;
    std::stop_token stop_token_;
    std::coroutine_handle<> handle_;
    std::optional<std::stop_callback<CancelFn>> callback_;

    // using callback_type = std::stop_callback<std::function<void()>>;

    // struct State {
    //     std::atomic<bool> alive{ true };
    //     std::coroutine_handle<> handle;
    // };

    // IOContext* context_;
    // std::stop_token token_;
    // std::shared_ptr<State> state_{ std::make_shared<State>() };
    // std::unique_ptr<callback_type> callback_;

public:
    StopRequestedAwaiter(IOContext& context, std::stop_token token)
      : context_(&context), stop_token_(std::move(token))
    {}

    // ~StopRequestedAwaiter()
    // {
    //     state_->alive.store(false, std::memory_order_release);
    //     callback_.reset();
    // }

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool 
    {
        return stop_token_.stop_requested(); 
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        callback_.emplace(stop_token_, CancelFn{ this });
        return true;
    }

    void await_resume() noexcept
    {
        callback_.reset();
    }
};

} // namespace async

#endif // BLOG_ASYNC_STOP_REQUESTED_AWAITER_H
