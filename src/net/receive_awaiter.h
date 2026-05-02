#ifndef BLOG_NET_RECEIVE_AWAITER_H
#define BLOG_NET_RECEIVE_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include <liburing.h>

#include <async.h>

namespace net {

/**
 * @brief Suspend until a single `recv` completes via io_uring.
 *
 * Submits one `io_uring_prep_recv` SQE and resumes the coroutine with the
 * number of bytes read, or an error code if the operation fails.
 * A result of 0 indicates the peer closed the connection.
 */
class ReceiveAwaiter: public async::Operation {
public:
    using resume_type = std::size_t;
    using context_type = async::IOContext;

    /**
     * @brief Construct with target fd and destination buffer.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Source socket file descriptor.
     * @param buffer  Writable byte span that receives data.
     * @pre `buffer` must remain valid until the coroutine is resumed.
     */
    ReceiveAwaiter(context_type& context, int fd, std::span<std::byte> buffer);

    ~ReceiveAwaiter() = default;

    [[nodiscard]]
    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept;

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
    std::size_t byte_read_{ 0 };
    int error_code_{ 0 };
};

} // namespace net

#endif // BLOG_NET_RECEIVE_AWAITER_H