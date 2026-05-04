#ifndef BLOG_ASYNC_POLL_AWAITER_H
#define BLOG_ASYNC_POLL_AWAITER_H

#include <liburing.h>

#include <single_operation.h>

namespace async {

/**
 * @brief Suspend until a file descriptor becomes ready for the requested events.
 *
 * Submits an `io_uring_prep_poll_add` SQE and resumes the calling coroutine
 * when the CQE arrives. Useful for waiting on file descriptors that do not
 * have a dedicated io_uring opcode (e.g. `signalfd`, `eventfd`).
 */
class PollAwaiter: public SingleOperation<PollAwaiter, void> {
public:
    /**
     * @brief Construct with the fd and event mask to poll.
     *
     * @param context I/O context that drives this operation.
     * @param fd      File descriptor to watch.
     * @param events  `POLLIN`/`POLLOUT`/... mask forwarded to `io_uring_prep_poll_add`.
     */
    PollAwaiter(context_type& context, int fd, short events)
      : SingleOperation<PollAwaiter, void>{ context },
        fd_{ fd }, 
        events_{ events }
    {}

    ~PollAwaiter() = default;

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_poll_add(sqe, fd_, events_);
    }

private:
    int fd_;
    short events_;
};

} // namespace async

#endif // BLOG_ASYNC_POLL_AWAITER_H