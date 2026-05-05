#ifndef BLOG_NET_SEND_ALL_AWAITER_H
#define BLOG_NET_SEND_ALL_AWAITER_H

#include <cstddef>
#include <span>

#include <sys/socket.h>

#include <liburing.h>

#include <async.h>

namespace net {

/**
 * @brief Suspend until an entire buffer has been sent via io_uring.
 *
 * Unlike a single-shot send awaiter, this awaiter retries until the full
 * span has been delivered or an error occurs. Partial writes resubmit the
 * remainder automatically without suspending the caller again.
 *
 * Derives from `LoopOperation` (which derives from `CancelableOperation`)
 * so it can be wrapped by `TimeoutCombinator`.
 */
class SendAllAwaiter: public async::LoopOperation<SendAllAwaiter, std::span<const std::byte>> {
public:
    /**
     * @brief Construct with target fd and full source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param socket  Destination socket file descriptor.
     * @param buffer  Read-only byte span to send in full.
     * @pre `buffer` must remain valid until the coroutine resumes.
     */
    SendAllAwaiter(async::IOContext& context, int socket, std::span<const std::byte> buffer)
      : async::LoopOperation<SendAllAwaiter, std::span<const std::byte>>{ context, buffer }
      , socket_{ socket }
    {}

    auto arm() noexcept -> bool
    {
        if (auto* sqe = context_->sqe()) {
            ::io_uring_prep_send(sqe, socket_, buffer_.data(), buffer_.size(), 0);
            ::io_uring_sqe_set_data(sqe, this);
            context_->track(this);
            return true;
        }

        error_code_ = EAGAIN;
        return false;
    }

private:
    int socket_;
};

} // namespace net

#endif // BLOG_NET_SEND_ALL_AWAITER_H