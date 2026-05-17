#ifndef BLOG_FILE_SYSTEM_READ_AWAITER_H
#define BLOG_FILE_SYSTEM_READ_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async/async.h>

namespace fs {

class ReadAwaiter : public async::IOAwaiter<ReadAwaiter, std::size_t> {
private:
    int fd_;
    std::span<std::byte> buffer_;
    std::uint64_t offset_{ 0 };

public:
    ReadAwaiter(int fd, std::span<std::byte> buffer, std::uint64_t offset = 0)
      : fd_{ fd }
      , buffer_{ buffer }
      , offset_{ offset }
    {}

    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_read(sqe, fd_, buffer_.data(), buffer_.size(), offset_);
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_READ_AWAITER_H