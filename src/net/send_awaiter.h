#ifndef BLOG_NET_SEND_AWAITER_H
#define BLOG_NET_SEND_AWAITER_H

#include <cstddef>
#include <span>

#include <liburing.h>

#include <async.h>

namespace net {

/**
 * @brief Suspend until a single `send` completes via io_uring.
 *
 * Submits one `io_uring_prep_send` SQE and resumes the coroutine with the
 * number of bytes sent, or an error code on failure.
 */
class SendAwaiter: public async::SingleOperation<SendAwaiter, std::size_t> {
public:
    SendAwaiter(context_type& context, int fd, std::span<const std::byte> buffer)
      : async::SingleOperation<SendAwaiter, std::size_t>{ context },
        fd_{ fd },
        buffer_{ buffer }
    {}

    ~SendAwaiter() = default;

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_send(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }

    auto result() noexcept -> std::size_t 
    { 
        return bytes_sent_; 
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        bytes_sent_ = static_cast<std::size_t>(result);
    }

private:
    int fd_;
    std::span<const std::byte> buffer_;
    std::size_t bytes_sent_{ 0 };
};

} // namespace net

#endif // BLOG_NET_SEND_AWAITER_H