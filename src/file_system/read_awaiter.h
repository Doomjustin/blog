#ifndef BLOG_FILE_SYSTEM_READ_AWAITER_H
#define BLOG_FILE_SYSTEM_READ_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async/async.h>

namespace fs {

/**
 * @brief Suspend until a single `read` completes via io_uring.
 *
 * Submits one `io_uring_prep_read` SQE using the file's current position
 * (offset -1) and resumes the coroutine with the number of bytes read,
 * or an error code on failure. A result of 0 indicates EOF.
 */
class ReadAwaiter: public async::SingleOperation<ReadAwaiter, std::size_t> {
public:
    ReadAwaiter(context_type& context, int fd, std::span<std::byte> buffer)
      : async::SingleOperation<ReadAwaiter, std::size_t>{ context },
        fd_{ fd },
        buffer_{ buffer }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_read(sqe, fd_, buffer_.data(), buffer_.size(), -1);
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
    std::span<std::byte> buffer_;
    std::size_t bytes_read_{ 0 };
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_READ_AWAITER_H
