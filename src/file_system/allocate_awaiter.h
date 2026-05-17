#ifndef BLOG_FILE_SYSTEM_ALLOCATE_AWAITER_H
#define BLOG_FILE_SYSTEM_ALLOCATE_AWAITER_H

#include <cstdint>

#include <liburing.h>

#include <async/async.h>

namespace fs {

class AllocateAwaiter : public async::IOAwaiter<AllocateAwaiter> {
private:
    int fd_;
    std::uint64_t offset_;
    std::uint64_t size_;
    int mode_{ 0 };

public:
    AllocateAwaiter(int fd, std::uint64_t offset, std::uint64_t size, int mode = 0)
      : fd_{ fd }
      , offset_{ offset }
      , size_{ size }
      , mode_{ mode }
    {}

    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_fallocate(sqe, fd_, mode_, offset_, size_);
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_ALLOCATE_AWAITER_H