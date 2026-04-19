#ifndef BLOG_POLL_AWAITER_H
#define BLOG_POLL_AWAITER_H

#include <coroutine>
#include <utility>

#include <liburing.h>

#include "exceptions.h"
#include "operation.h"

template<typename Context>
class PollAwaiter: public Operation {
public:
    using resume_type = void;

    PollAwaiter(Context& context, int fd, short events)
      : context_{ context }, 
        fd_{ fd }, 
        events_{ events }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> handle) -> void
    {
        handle_ = handle;

        auto* sqe = context_.sqe();

        prepare(sqe);
        ::io_uring_sqe_set_data(sqe, this);
    }

    auto await_resume() -> std::expected<void, std::error_code>
    {
        if (error_code_)
            return unexpected_system_error(error_code_);

        return {};
    }

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_poll_add(sqe, fd_, events_);
    }

    void set_result(int result, [[maybe_unused]] std::uint32_t flags) noexcept
    {
        error_code_ = result < 0 ? -result : 0;
    }

    void complete(int res, std::uint32_t flags) noexcept override
    {
        set_result(res, flags);
        
        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

    auto context() noexcept -> Context& { return context_; }

private:
    Context& context_;
    int fd_;
    short events_;

    std::coroutine_handle<> handle_{ nullptr };
    int error_code_{ 0 };
};

#endif // BLOG_POLL_AWAITER_H