#ifndef BLOG_FILE_SYSTEM_WRITE_AT_AWAITER_H
#define BLOG_FILE_SYSTEM_WRITE_AT_AWAITER_H

#include <cstddef>
#include <cstdint>
#include <span>

#include <liburing.h>

#include <async/single_operation.h>

namespace fs {

/**
 * @brief Suspend until a positional `write` completes via io_uring.
 *
 * Submits one `io_uring_prep_write` SQE with an explicit byte offset and
 * resumes the coroutine with the number of bytes written, or an error code
 * on failure. The file's current position is not modified.
 */
class WriteAtAwaiter: public async::SingleOperation<WriteAtAwaiter, std::size_t> {
public:
    WriteAtAwaiter(context_type& context, int fd, std::uint64_t offset, std::span<const std::byte> buffer)
      : async::SingleOperation<WriteAtAwaiter, std::size_t>{ context },
        fd_{ fd },
        offset_{ offset },
        buffer_{ buffer }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_write(sqe, fd_, buffer_.data(), buffer_.size(), offset_);
    }

    auto result() noexcept -> std::size_t
    {
        return bytes_written_;
    }

    void set_result(int result, std::uint32_t /*flags*/) noexcept
    {
        bytes_written_ = static_cast<std::size_t>(result);
    }

private:
    int fd_;
    std::uint64_t offset_;
    std::span<const std::byte> buffer_;
    std::size_t bytes_written_{ 0 };
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_WRITE_AT_AWAITER_H
