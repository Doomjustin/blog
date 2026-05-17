#ifndef BLOG_ASYNC_CLOSE_AWAITER_H
#define BLOG_ASYNC_CLOSE_AWAITER_H

#include "io_awaiter.h"

namespace async {

class CloseAwaiter : public IOAwaiter<CloseAwaiter> {
private:
    int fd_;

public:
    explicit CloseAwaiter(int fd) noexcept
      : fd_{ fd }
    {}

    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_close(sqe, fd_);
    }
};

} // namespace async

#endif // BLOG_ASYNC_CLOSE_AWAITER_H