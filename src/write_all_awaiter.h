#ifndef BLOG_WRITE_ALL_WAITER_H
#define BLOG_WRITE_ALL_WAITER_H

#include <cerrno>
#include <coroutine>
#include <cstddef>
#include <span>

#include <sys/socket.h>

#include <liburing.h>

#include "exceptions.h"
#include "operation.h"

template<typename Context>
class WriteAllAwaiter: public CancelableOperation {
public:
    using resume_type = std::size_t;
    using context_type = Context;

    WriteAllAwaiter(context_type& context, int socket, std::span<const std::byte> buffer)
      : context_{ context }, 
        socket_{ socket }, 
        buffer_{ buffer }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;
        arm_write();
    }

    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        return bytes_written_;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (error_code_ != 0 || buffer_.empty())
            resume(handle_, result, flags);
        else
            arm_write();
    }

    auto context() noexcept -> context_type& { return context_; }

private:
    Context& context_;
    int socket_;
    std::span<const std::byte> buffer_;
    std::size_t expected_to_write_{ buffer_.size() };

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t bytes_written_{ 0 };
    int error_code_{ 0 };

    void arm_write() noexcept
    {
        auto* sqe = context_.sqe();

        ::io_uring_prep_send(sqe, socket_, buffer_.data(), buffer_.size(), 0);
        ::io_uring_sqe_set_data(sqe, this);
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result > 0) {
            bytes_written_ += static_cast<std::size_t>(result);

            buffer_ = buffer_.subspan(result);
        }
        else if (result == 0 && bytes_written_ > 0) {
            error_code_ = ECONNABORTED;
        }
        else
            error_code_ = -result;
    }
};

#endif // BLOG_WRITE_ALL_WAITER_H