#ifndef BLOG_NET_SEND_ALL_ZC_AWAITER_H
#define BLOG_NET_SEND_ALL_ZC_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <system_error>

#include <liburing.h>

#include <async.h>

namespace net {

/**
 * @brief Suspend until an entire buffer has been sent via zero-copy io_uring.
 *
 * Issues repeated `IORING_OP_SEND_ZC` operations until the full span has
 * been delivered or an error occurs. Each zero-copy send produces two CQEs:
 * a send CQE (carrying the byte count) and a notif CQE (signaling that the
 * kernel has released its reference to the buffer). The next partial send
 * is only submitted after the notif CQE arrives, ensuring the kernel no
 * longer reads the previous slice before the pointer is advanced.
 *
 * The buffer must remain valid until the coroutine resumes (i.e., until
 * the final notif CQE is processed).
 *
 * Derives from `CancelableOperation` so it can be wrapped by
 * `TimeoutCombinator`.
 */
class SendAllZCAwaiter: public async::CancelableOperation {
public:
    using resume_type = std::size_t;
    using context_type = async::IOContext;

    /**
     * @brief Construct with target fd and full source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param socket  Destination socket file descriptor.
     * @param buffer  Read-only byte span to send in full.
     * @pre `buffer` must remain valid until the coroutine resumes.
     */
    SendAllZCAwaiter(context_type& context, int socket, std::span<const std::byte> buffer);

    ~SendAllZCAwaiter() = default;

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
    context_type& context_;
    int socket_;
    std::span<const std::byte> buffer_;

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t bytes_written_{ 0 };
    int error_code_{ 0 };

    void arm_zc_write() noexcept;

    void set_result(int result, std::uint32_t flags) noexcept;
};

} // namespace net

#endif // BLOG_NET_SEND_ALL_ZC_AWAITER_H