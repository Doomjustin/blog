#ifndef BLOG_FILE_SYSTEM_READ_ALL_AWAITER_H
#define BLOG_FILE_SYSTEM_READ_ALL_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async/async.h>

namespace fs {

/**
 * @brief Suspend until an entire buffer has been filled from a file via io_uring.
 *
 * Retries `read` automatically on partial reads until the buffer is fully
 * filled or EOF is reached. Unlike the socket counterpart, EOF is not
 * treated as an error: the coroutine resumes with however many bytes were
 * read before the file ended.
 *
 * Derives from `LoopOperation` so it can be wrapped by `TimeoutCombinator`.
 */
class ReadAllAwaiter: public async::LoopOperation<ReadAllAwaiter, std::span<std::byte>> {
public:
    /**
     * @brief Construct with target fd and destination buffer.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Source file descriptor.
     * @param buffer  Writable byte span to fill.
     * @pre `buffer` must remain valid until the coroutine resumes.
     */
    ReadAllAwaiter(async::IOContext& context, int fd, std::span<std::byte> buffer)
      : async::LoopOperation<ReadAllAwaiter, std::span<std::byte>>{ context, buffer }
      , fd_{ fd }
    {}

    auto arm() noexcept -> bool
    {
        if (auto* sqe = context_->sqe()) {
            ::io_uring_prep_read(sqe, fd_, buffer_.data(), buffer_.size(), -1);
            ::io_uring_sqe_set_data(sqe, this);
            context_->track(this);
            return true;
        }

        error_code_ = EAGAIN;
        return false;
    }

    /**
     * @brief Override to treat EOF (result == 0) as a normal stop condition.
     *
     * The base class maps result==0 to ECONNABORTED, which is wrong for
     * files. When EOF arrives before the buffer is full, we stop and return
     * `bytes_processed_` rather than an error.
     */
    void complete(int result, std::uint32_t flags) noexcept override
    {
        context_->untrack(this);

        if (result == 0) {
            resume(handle_, 0, 0);
            return;
        }

        set_result(result, flags);
        finish_or_rearm(result, flags);
    }

private:
    int fd_;
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_READ_ALL_AWAITER_H
