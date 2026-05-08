#ifndef BLOG_FILE_SYSTEM_CLOSE_AWAITER_H
#define BLOG_FILE_SYSTEM_CLOSE_AWAITER_H

#include <liburing.h>

#include <async/async.h>

namespace fs {

/**
 * @brief Suspend until a `close` completes via io_uring.
 *
 * Submits one `io_uring_prep_close` SQE and resumes the coroutine once
 * the file descriptor has been closed by the kernel.  The file descriptor
 * must already have been removed from the owning object before constructing
 * this awaiter (i.e. the caller should `std::exchange` the fd to -1 first).
 */
class CloseAwaiter: public async::SingleOperation<CloseAwaiter, void> {
public:
    CloseAwaiter(context_type& context, int fd)
      : async::SingleOperation<CloseAwaiter, void>{ context },
        fd_{ fd }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_close(sqe, fd_);
    }

private:
    int fd_;
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_CLOSE_AWAITER_H
