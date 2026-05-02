#ifndef BLOG_NET_SEND_ZC_AWAITER_H
#define BLOG_NET_SEND_ZC_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>

#include <async.h>

namespace net {

/**
 * @brief Suspend until a single zero-copy `send` completes via io_uring.
 *
 * Uses `IORING_OP_SEND_ZC` to avoid copying the send buffer into the
 * kernel. The caller must keep the buffer alive until the notif CQE
 * arrives (signaled by the `IORING_CQE_F_NOTIF` flag in the second CQE).
 * A result of 0 in `await_resume` is valid and indicates the notif has
 * been received.
 */
class SendZCAwaiter: public async::Operation {
public:
    using resume_type = std::size_t;
    using context_type = async::IOContext;

    /**
     * @brief Construct with target fd and source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Destination socket file descriptor.
     * @param buffer  Read-only byte span; must remain valid until the notif CQE.
     */
    SendZCAwaiter(context_type& context, int fd, std::span<const std::byte> buffer);

    ~SendZCAwaiter() = default;

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

    auto context() noexcept -> context_type& { return context_; }

private:
    context_type& context_;
    int fd_;
    std::span<const std::byte> buffer_;

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t byte_sent_{ 0 };
    int error_code_{ 0 };
};

} // namespace net

#endif // BLOG_NET_SEND_ZC_AWAITER_H