#ifndef BLOG_FILE_SYSTEM_TRUNCATE_AWAITER_H
#define BLOG_FILE_SYSTEM_TRUNCATE_AWAITER_H

#include <cstdint>

#include <liburing.h>

#include <async/async.h>

namespace fs {

class TruncateAwaiter : public async::IOAwaiter<TruncateAwaiter> {
private:
    int fd_;
    std::uint64_t new_size_;

public:
    TruncateAwaiter(int fd, std::uint64_t new_size)
      : fd_{ fd }
      , new_size_{ new_size }
    {}

    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_ftruncate(sqe, fd_, new_size_);
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_TRUNCATE_AWAITER_H