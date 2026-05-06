#ifndef BLOG_NET_SEND_ZC_AWAITER_H
#define BLOG_NET_SEND_ZC_AWAITER_H

#include <cstddef>
#include <type_traits>

#include <liburing.h>

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
class SendZCAwaiter: public async::SingleOperation<SendZCAwaiter, std::size_t> {
public:
    using is_single_shot = std::false_type;

    /**
     * @brief Construct with target fd and source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Destination socket file descriptor.
     * @param buffer  Read-only byte span; must remain valid until the notif CQE.
     */
    SendZCAwaiter(context_type& context, int fd, std::span<const std::byte> buffer)
      : async::SingleOperation<SendZCAwaiter, std::size_t>{ context },
        fd_{ fd },
        buffer_{ buffer }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_send_zc(sqe, fd_, buffer_.data(), buffer_.size(), 0, 0);
    }

    void set_result(int result, std::uint32_t /*flags*/) noexcept
    {
        byte_sent_ = static_cast<std::size_t>(result);
    }

    auto result() noexcept -> std::size_t
    {
        return byte_sent_;
    }

    /**
     * @brief Override to handle the two-CQE send_zc protocol.
     *
     * io_uring delivers two CQEs for a zero-copy send:
     * 1. The send result CQE (no special flags): carries bytes sent or error.
     * 2. The notif CQE (`IORING_CQE_F_NOTIF`): signals buffer release.
     * The coroutine is resumed only after both CQEs arrive, indicated by
     * the absence of `IORING_CQE_F_MORE` on the final CQE.
     */
    void complete(int result, std::uint32_t flags) noexcept override
    {
        if (!(flags & IORING_CQE_F_NOTIF)) {
            if (result < 0)
                error_code_ = -result;
            else
                set_result(result, flags);
        }

        if (!(flags & IORING_CQE_F_MORE)) {
            context().untrack(this);
            this->resume(handle_, result, flags);
        }
    }

private:
    int fd_;
    std::span<const std::byte> buffer_;
    
    std::size_t byte_sent_{ 0 };
};

} // namespace net

#endif // BLOG_NET_SEND_ZC_AWAITER_H