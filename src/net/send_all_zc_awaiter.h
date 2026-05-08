#ifndef BLOG_NET_SEND_ALL_ZC_AWAITER_H
#define BLOG_NET_SEND_ALL_ZC_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>
#include <liburing/io_uring.h>

#include <async/async.h>

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
 * Derives from `LoopOperation` (which derives from `CancelableOperation`)
 * so it can be wrapped by `TimeoutCombinator`.
 */
class SendAllZCAwaiter: public async::LoopOperation<SendAllZCAwaiter, std::span<const std::byte>> {
public:
    /**
     * @brief Construct with target fd and full source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param socket  Destination socket file descriptor.
     * @param buffer  Read-only byte span to send in full.
     * @pre `buffer` must remain valid until the coroutine resumes.
     */
    SendAllZCAwaiter(async::IOContext& context, int socket, std::span<const std::byte> buffer)
      : async::LoopOperation<SendAllZCAwaiter, std::span<const std::byte>>{ context, buffer }
      , socket_{ socket }
    {}

    auto arm() noexcept -> bool
    {
        if (auto* sqe = context_->sqe()) {
            ::io_uring_prep_send_zc(sqe, socket_, buffer_.data(), buffer_.size(), 0, 0);
            ::io_uring_sqe_set_data(sqe, this);
            context_->track(this);
            return true;
        }

        error_code_ = EAGAIN;
        return false;
    }

    /**
     * @brief Override to handle the two-CQE send_zc protocol.
     *
     * io_uring delivers two CQEs per zero-copy send:
     * 1. Send result CQE (no special flags): carries bytes sent or error.
     * 2. Notif CQE (`IORING_CQE_F_NOTIF`): signals that the kernel has
     *    released its reference to the buffer slice.
     * `IORING_CQE_F_MORE` is set on the send CQE while the notif is still
     * pending; the retry decision is deferred until both CQEs arrive.
     */
    void complete(int result, std::uint32_t flags) noexcept override
    {
        if (!(flags & IORING_CQE_F_NOTIF))
            set_result(result, flags);

        if (!(flags & IORING_CQE_F_MORE)) {
            context_->untrack(this);
            finish_or_rearm(result, flags);
        }
    }

private:
    int socket_;
};

} // namespace net

#endif // BLOG_NET_SEND_ALL_ZC_AWAITER_H