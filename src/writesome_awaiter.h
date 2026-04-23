#ifndef BLOG_WRITESOME_AWAITER_H
#define BLOG_WRITESOME_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>
#include <span>
#include <utility>

#include <liburing.h>

#include "exceptions.h"
#include "operation.h"

/**
 * @brief Suspend until a single `send` completes via io_uring.
 *
 * Submits one `io_uring_prep_send` SQE and resumes the coroutine with the
 * number of bytes sent, or an error code on failure.
 *
 * @tparam Context Execution context type (must provide `sqe()`).
 */
template<typename Context>
class WriteSomeAwaiter: public Operation {
public:
    using resume_type = std::size_t;

    /**
     * @brief Construct with target fd and source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Destination socket file descriptor.
     * @param buffer  Read-only byte span of data to send.
     * @pre `buffer` must remain valid until the coroutine is resumed.
     */
    WriteSomeAwaiter(Context& context, int fd, std::span<const std::byte> buffer)
      : context_{ context }, fd_{ fd }, buffer_{ buffer }
    {}

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

        return byte_writted_;
    }

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_send(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result >= 0)
            byte_writted_ = static_cast<std::size_t>(result);
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

    auto context() noexcept -> Context& { return context_; }

private:
    std::coroutine_handle<> handle_{ nullptr };
    Context& context_;
    int fd_;
    std::span<const std::byte> buffer_;
    std::size_t byte_writted_ = -1;
    int error_code_ = 0;
};

#endif // BLOG_WRITESOME_AWAITER_H