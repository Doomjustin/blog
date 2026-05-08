#ifndef BLOG_FILE_SYSTEM_READ_AT_AWAITER_H
#define BLOG_FILE_SYSTEM_READ_AT_AWAITER_H

#include <cstddef>
#include <cstdint>
#include <span>

#include <liburing.h>

#include <async/async.h>

namespace fs {

/**
 * @brief Suspend until a positional `read` completes via io_uring.
 *
 * Submits one `io_uring_prep_read` SQE with an explicit byte offset and
 * resumes the coroutine with the number of bytes read, or an error code on
 * failure. The file's current position is not modified. A result of 0
 * indicates EOF at the given offset.
 */
class ReadAtAwaiter: public async::SingleOperation<ReadAtAwaiter, std::size_t> {
public:
    ReadAtAwaiter(context_type& context, int fd, std::uint64_t offset, std::span<std::byte> buffer)
      : async::SingleOperation<ReadAtAwaiter, std::size_t>{ context },
        fd_{ fd },
        offset_{ offset },
        buffer_{ buffer }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_read(sqe, fd_, buffer_.data(), buffer_.size(), offset_);
    }

    auto result() noexcept -> std::size_t
    {
        return bytes_read_;
    }

    void set_result(int result, std::uint32_t /*flags*/) noexcept
    {
        bytes_read_ = static_cast<std::size_t>(result);
    }

private:
    int fd_;
    std::uint64_t offset_;
    std::span<std::byte> buffer_;
    std::size_t bytes_read_{ 0 };
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_READ_AT_AWAITER_H
