#ifndef BLOG_READ_ALL_AWAITER_H
#define BLOG_READ_ALL_AWAITER_H

#include <coroutine>
#include <cstdint>
#include <span>

#include <liburing.h>

#include "exceptions.h"
#include "operation.h"

/**
 * @brief Suspend until an entire buffer has been filled via io_uring.
 *
 * Retries `recv` automatically on partial reads until the provided buffer
 * is completely filled or the operation fails. If the peer closes the
 * connection at any point before the buffer is full, `ECONNABORTED` is
 * returned regardless of how many bytes were already received.
 *
 * Derives from `CancelableOperation` so it can be wrapped by
 * `TimeoutCombinator`; the `parent` pointer routes completions through the
 * combinator when a timeout is active.
 *
 * @tparam Context Execution context type (must provide `sqe()`).
 */
template<typename Context>
class ReadAllAwaiter: public CancelableOperation {
public:
    using resume_type = std::size_t;
    using context_type = Context;

    ReadAllAwaiter(context_type& context, int socket, std::span<std::byte> buffer)
      : context_{ context },
        socket_{ socket },
        buffer_{ buffer }
    {}

    ~ReadAllAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;
        arm_read();
    }

    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        return bytes_read_;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (error_code_ != 0 || buffer_.empty())
            resume(handle_, result, flags);
        else
            arm_read();
    }

    [[nodiscard]]
    auto context() noexcept -> context_type& { return context_; }

private:
    context_type& context_;
    int socket_;
    std::span<std::byte> buffer_;
    std::size_t expected_to_read_{ buffer_.size() };

    std::coroutine_handle<> handle_;
    std::size_t bytes_read_{ 0 };
    int error_code_{ 0 };

    void arm_read() noexcept
    {
        auto* sqe = context_.sqe();

        ::io_uring_prep_recv(sqe, socket_, buffer_.data(), buffer_.size(), 0);
        ::io_uring_sqe_set_data(sqe, this);
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result > 0) {
            bytes_read_ += static_cast<std::size_t>(result);
            buffer_ = buffer_.subspan(result);
        }
        else if (result == 0) {
            error_code_ = ECONNABORTED;
        }
        else {
            error_code_ = -result;
        }
    }
};

#endif // BLOG_READ_ALL_AWAITER_H