#ifndef BLOG_FILE_SYSTEM_WRITE_ALL_AWAITER_H
#define BLOG_FILE_SYSTEM_WRITE_ALL_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async/loop_operation.h>

namespace fs {

/**
 * @brief Suspend until an entire buffer has been written to a file via io_uring.
 *
 * Retries `write` automatically on partial writes until the full span has
 * been delivered to the kernel or an error occurs.
 *
 * Derives from `LoopOperation` so it can be wrapped by `TimeoutCombinator`.
 */
class WriteAllAwaiter: public async::LoopOperation<WriteAllAwaiter, std::span<const std::byte>> {
public:
    /**
     * @brief Construct with target fd and source buffer.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Destination file descriptor.
     * @param buffer  Read-only byte span to write in full.
     * @pre `buffer` must remain valid until the coroutine resumes.
     */
    WriteAllAwaiter(async::IOContext& context, int fd, std::span<const std::byte> buffer)
      : async::LoopOperation<WriteAllAwaiter, std::span<const std::byte>>{ context, buffer }, 
        fd_{ fd }
    {}

    auto arm() noexcept -> bool
    {
        if (auto* sqe = context_->sqe()) {
            ::io_uring_prep_write(sqe, fd_, buffer_.data(), buffer_.size(), -1);
            ::io_uring_sqe_set_data(sqe, this);
            context_->track(this);
            return true;
        }

        error_code_ = EAGAIN;
        return false;
    }

private:
    int fd_;
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_WRITE_ALL_AWAITER_H
