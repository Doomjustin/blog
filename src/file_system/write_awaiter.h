#ifndef BLOG_FILE_SYSTEM_WRITE_AWAITER_H
#define BLOG_FILE_SYSTEM_WRITE_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async/async.h>

namespace fs {

class WriteAwaiter : public async::IOAwaiter<WriteAwaiter, std::size_t> {
private:
    int fd_;
    std::span<const std::byte> buffer_;

public:
    WriteAwaiter(int fd, std::span<const std::byte> buffer)
      : fd_{ fd }
      , buffer_{ buffer }
    {}

    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_write(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }

    auto value() noexcept -> std::size_t
    {
        return static_cast<std::size_t>(this->result);
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_WRITE_AWAITER_H