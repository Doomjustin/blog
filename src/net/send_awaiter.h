#ifndef BLOG_NET_SEND_AWAITER_H
#define BLOG_NET_SEND_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include <liburing.h>

#include <async.h>

namespace net {

/**
 * @brief Suspend until a single `send` completes via io_uring.
 *
 * Submits one `io_uring_prep_send` SQE and resumes the coroutine with the
 * number of bytes sent, or an error code on failure.
 */
class SendAwaiter: public async::Operation {
public:
    using resume_type = std::size_t;
    using context_type = async::IOContext;

    /**
     * @brief Construct with target fd and source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Destination socket file descriptor.
     * @param buffer  Read-only byte span of data to send.
     * @pre `buffer` must remain valid until the coroutine is resumed.
     */
    SendAwaiter(context_type& context, int fd, std::span<const std::byte> buffer);

    ~SendAwaiter() = default;

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
    std::span<const std::byte> buffer_;

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t byte_sent_{ 0 };
    int error_code_ = 0;
};

} // namespace net

#endif // BLOG_NET_SEND_AWAITER_H