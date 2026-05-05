#ifndef BLOG_NET_RECEIVE_AWAITER_H
#define BLOG_NET_RECEIVE_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async.h>

namespace net {

/**
 * @brief Suspend until a single `recv` completes via io_uring.
 *
 * Submits one `io_uring_prep_recv` SQE and resumes the coroutine with the
 * number of bytes read, or an error code if the operation fails.
 * A result of 0 indicates the peer closed the connection.
 */
class ReceiveAwaiter: public async::SingleOperation<ReceiveAwaiter, std::size_t> {
public:
    ReceiveAwaiter(context_type& context, int fd, std::span<std::byte> buffer)
      : async::SingleOperation<ReceiveAwaiter, std::size_t>{ context },
        fd_{ fd },
        buffer_{ buffer }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_recv(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }

    auto result() noexcept -> std::size_t 
    { 
        return bytes_read_; 
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        bytes_read_ = static_cast<std::size_t>(result);
    }

private:
    int fd_;
    std::span<std::byte> buffer_;
    std::size_t bytes_read_{ 0 };
};

} // namespace net

#endif // BLOG_NET_RECEIVE_AWAITER_H