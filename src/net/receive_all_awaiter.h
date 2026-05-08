#ifndef BLOG_NET_RECEIVE_ALL_AWAITER_H
#define BLOG_NET_RECEIVE_ALL_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async/async.h>

namespace net {

/**
 * @brief Suspend until an entire buffer has been filled via io_uring.
 *
 * Retries `recv` automatically on partial reads until the provided buffer
 * is completely filled or the operation fails. If the peer closes the
 * connection at any point before the buffer is full, `ECONNABORTED` is
 * returned regardless of how many bytes were already received.
 *
 * Derives from `LoopOperation` (which derives from `CancelableOperation`)
 * so it can be wrapped by `TimeoutCombinator`.
 */
class ReceiveAllAwaiter: public async::LoopOperation<ReceiveAllAwaiter, std::span<std::byte>> {
public:
    /**
     * @brief Construct with target fd and destination buffer.
     *
     * @param context I/O context that drives this operation.
     * @param socket  Source socket file descriptor.
     * @param buffer  Writable byte span to fill entirely.
     * @pre `buffer` must remain valid until the coroutine resumes.
     */
    ReceiveAllAwaiter(async::IOContext& context, int socket, std::span<std::byte> buffer)
      : async::LoopOperation<ReceiveAllAwaiter, std::span<std::byte>>{ context, buffer }
      , socket_{ socket }
    {}

    auto arm() noexcept -> bool
    {
        if (auto* sqe = context_->sqe()) {
            ::io_uring_prep_recv(sqe, socket_, buffer_.data(), buffer_.size(), 0);
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

#endif // BLOG_NET_RECEIVE_ALL_AWAITER_H