#ifndef BLOG_ASYNC_READ_AWAITER_H
#define BLOG_ASYNC_READ_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include <liburing.h>

#include <io_context.h>
#include <operation.h>

namespace async {

/**
 * @brief Suspend until a single `read` completes via io_uring.
 *
 * Submits one `io_uring_prep_read` SQE and resumes the coroutine with the
 * number of bytes read, or an error code on failure.
 * A result of 0 indicates EOF.
 */
class ReadAwaiter: public Operation {
public:
    using resume_type = std::size_t;
    using context_type = IOContext;

    ReadAwaiter(context_type& context, int fd, std::span<std::byte> buffer);

    ~ReadAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool;

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>;

    void prepare(::io_uring_sqe* sqe) noexcept;

    void set_result(int result, std::uint32_t flags) noexcept;

    void complete(int result, std::uint32_t flags) noexcept override;

    auto context() noexcept -> context_type& { return context_; }

private:
    context_type& context_;
    int fd_;
    std::span<std::byte> buffer_;

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t bytes_read_{ 0 };
    int error_code_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_READ_AWAITER_H
