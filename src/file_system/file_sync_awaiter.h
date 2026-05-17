#ifndef BLOG_FILE_SYSTEM_FILE_SYNC_AWAITER_H
#define BLOG_FILE_SYSTEM_FILE_SYNC_AWAITER_H

#include <liburing.h>

#include <async/async.h>

namespace fs {

class FileSyncAwaiter : public async::IOAwaiter<FileSyncAwaiter> {
private:
    int fd_;
    unsigned flags_;

public:
    FileSyncAwaiter(int fd, unsigned flags = 0)
      : fd_{ fd }
      , flags_{ flags }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_fsync(sqe, fd_, flags_);
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_FILE_SYNC_AWAITER_H