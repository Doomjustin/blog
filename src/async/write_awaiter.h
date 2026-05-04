#ifndef BLOG_ASYNC_WRITE_AWAITER_H
#define BLOG_ASYNC_WRITE_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <single_operation.h>

namespace async {

/**
 * @brief Suspend until a single `write` completes via io_uring.
 *
 * Submits one `io_uring_prep_write` SQE and resumes the coroutine with the
 * number of bytes written, or an error code on failure.
 */
class WriteAwaiter: public SingleOperation<WriteAwaiter, std::size_t> {
public:
    WriteAwaiter(context_type& context, int fd, std::span<const std::byte> buffer)
      : SingleOperation<WriteAwaiter, std::size_t>{ context },
        fd_{ fd },
        buffer_{ buffer }
    {}

    ~WriteAwaiter() = default;

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_write(sqe, fd_, buffer_.data(), buffer_.size(), -1);
    }

    auto result() noexcept -> std::size_t 
    { 
        return bytes_written_; 
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        bytes_written_ = static_cast<std::size_t>(result);
    }

private:
    int fd_;
    std::span<const std::byte> buffer_;
    std::size_t bytes_written_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_WRITE_AWAITER_H