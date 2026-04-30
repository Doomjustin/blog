#ifndef BLOG_ASYNC_WRITE_SOME_AWAITER_H
#define BLOG_ASYNC_WRITE_SOME_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include <liburing.h>

#include "io_context.h"
#include "operation.h"

namespace async {

/**
 * @brief Suspend until a single `send` completes via io_uring.
 *
 * Submits one `io_uring_prep_send` SQE and resumes the coroutine with the
 * number of bytes sent, or an error code on failure.
 */
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
    WriteSomeAwaiter(IOContext& context, int fd, std::span<const std::byte> buffer);

    ~WriteSomeAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept;

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>;

    void prepare(::io_uring_sqe* sqe) noexcept;

    void set_result(int result, std::uint32_t flags) noexcept;

    void complete(int result, std::uint32_t flags) noexcept override;

    auto context() noexcept -> IOContext& { return context_; }

private:
    std::coroutine_handle<> handle_{ nullptr };
    IOContext& context_;
    int fd_;
    std::span<const std::byte> buffer_;
    std::size_t byte_writted_ = -1;
    int error_code_ = 0;
};

} // namespace async

#endif // BLOG_ASYNC_WRITE_SOME_AWAITER_H