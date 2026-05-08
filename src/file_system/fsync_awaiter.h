#ifndef BLOG_FILE_SYSTEM_FSYNC_AWAITER_H
#define BLOG_FILE_SYSTEM_FSYNC_AWAITER_H

#include <cstdint>

#include <liburing.h>

#include <async/async.h>

namespace fs {

/**
 * @brief Suspend until an `fsync` / `fdatasync` completes via io_uring.
 *
 * Submits one `io_uring_prep_fsync` SQE and resumes the coroutine once the
 * kernel has flushed the file data (and, optionally, metadata) to stable
 * storage.
 *
 * Pass `flags = IORING_FSYNC_DATASYNC` to request data-only sync (equivalent
 * to `fdatasync(2)`); leave it at `0` for a full `fsync(2)`.
 */
class FsyncAwaiter: public async::SingleOperation<FsyncAwaiter, void> {
public:
    FsyncAwaiter(context_type& context, int fd, std::uint32_t fsync_flags = 0) noexcept
      : async::SingleOperation<FsyncAwaiter, void>{ context },
        fd_{ fd },
        fsync_flags_{ fsync_flags }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_fsync(sqe, fd_, fsync_flags_);
    }

private:
    int fd_;
    std::uint32_t fsync_flags_;
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_FSYNC_AWAITER_H
