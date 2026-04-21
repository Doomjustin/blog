#ifndef BLOG_ACCEPT_AWAITER_H
#define BLOG_ACCEPT_AWAITER_H

#include <coroutine>
#include <expected>
#include <utility>

#include <liburing.h>

#include "exceptions.h"
#include "operation.h"

template<typename Protocol, typename Context>
class AcceptAwaiter: public Operation {
public:
    using socket_type = typename Protocol::template socket<Context>;
    using endpoint_type = typename Protocol::endpoint;
    using context_type = Context;
    using resume_type = socket_type;

    AcceptAwaiter(context_type& context, int fd, endpoint_type* peer = nullptr)
      : context_{ context }, fd_{ fd }, peer_{ peer }
    {
        if (peer_)
            addrlen_ = peer_->capacity();
    } 

    [[nodiscard]] 
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;

        auto* sqe = context_.sqe();
        
        prepare(sqe);
        ::io_uring_sqe_set_data(sqe, this);
    }

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        return resume_type{ context_, result_fd_ };
    }

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_accept(sqe, 
                               fd_, 
                               peer_ ? peer_->data() : nullptr, 
                               peer_ ? &addrlen_ : nullptr, 
                               0
                        );
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result >= 0)
            result_fd_ = result;
        else
            error_code_ = -result;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

    auto context() noexcept -> context_type& { return context_; }

private:
    context_type& context_;
    int fd_;
    endpoint_type* peer_;
    socklen_t addrlen_;

    std::coroutine_handle<> handle_{ nullptr };
    int result_fd_{ -1 };
    int error_code_{ 0 };
};

#endif // BLOG_ACCEPT_AWAITER_H