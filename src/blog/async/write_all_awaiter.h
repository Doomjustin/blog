#ifndef BLOG_ASYNC_WRITE_ALL_AWAITER_H
#define BLOG_ASYNC_WRITE_ALL_AWAITER_H

#include <cerrno>
#include <coroutine>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include <sys/socket.h>

#include <liburing.h>

#include "io_context.h"
#include "operation.h"

namespace async {

/**
 * @brief Suspend until an entire buffer has been sent via io_uring.
 *
 * Unlike `WriteSomeAwaiter`, which issues a single `send` and returns
 * however many bytes were accepted, this awaiter retries until the full
 * span has been delivered or an error occurs. Partial writes resubmit the
 * remainder automatically without suspending the caller again.
 *
 * Derives from `CancelableOperation` so it can be wrapped by
 * `TimeoutCombinator`; the `parent` pointer routes completions through the
 * combinator when a timeout is active.
 */
class WriteAllAwaiter: public CancelableOperation {
public:
    using resume_type = std::size_t;
    using context_type = IOContext;

    WriteAllAwaiter(context_type& context, int socket, std::span<const std::byte> buffer);

    ~WriteAllAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept;

    auto await_resume() -> std::expected<resume_type, std::error_code>;

    void complete(int result, std::uint32_t flags) noexcept override;

    auto context() noexcept -> context_type& { return context_; }

private:
    IOContext& context_;
    int socket_;
    std::span<const std::byte> buffer_;
    std::size_t expected_to_write_{ buffer_.size() };

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t bytes_written_{ 0 };
    int error_code_{ 0 };

    void arm_write() noexcept;

    void set_result(int result, std::uint32_t flags) noexcept;
};

} // namespace async

#endif // BLOG_ASYNC_WRITE_ALL_AWAITER_H