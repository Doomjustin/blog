#ifndef BLOG_NET_RECEIVE_ALL_AWAITER_H
#define BLOG_NET_RECEIVE_ALL_AWAITER_H

#include <coroutine>
#include <cstdint>
#include <expected>
#include <span>
#include <system_error>

#include <liburing.h>

#include <async.h>

namespace net {
    
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
 */
class ReceiveAllAwaiter: public async::CancelableOperation {
public:
    using resume_type = std::size_t;
    using context_type = async::IOContext;

    /**
     * @brief Construct with target fd and destination buffer.
     *
     * @param context I/O context that drives this operation.
     * @param socket  Source socket file descriptor.
     * @param buffer  Writable byte span to fill entirely.
     * @pre `buffer` must remain valid until the coroutine resumes.
     */
    ReceiveAllAwaiter(context_type& context, int socket, std::span<std::byte> buffer);

    ~ReceiveAllAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept;

    auto await_resume() -> std::expected<resume_type, std::error_code>;

    void complete(int result, std::uint32_t flags) noexcept override;

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

    void arm_read() noexcept;

    void set_result(int result, std::uint32_t flags) noexcept;
};

} // namespace net

#endif // BLOG_NET_RECEIVE_ALL_AWAITER_H