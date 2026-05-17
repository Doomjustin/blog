#ifndef BLOG_NET_RECEIVE_MESSAGE_AWAITER_H
#define BLOG_NET_RECEIVE_MESSAGE_AWAITER_H

#include <liburing.h>

#include <async/async.h>

namespace net {

template<typename Protocol>
class ReceiveMessageAwaiter
  : public async::IOAwaiter<ReceiveMessageAwaiter<Protocol>, std::size_t> {
public:
    using endpoint_type = typename Protocol::endpoint;

private:
    int fd_;
    std::span<std::byte> buffer_;
    endpoint_type* endpoint_;

public:
    ReceiveMessageAwaiter(int fd, std::span<std::byte> buffer, endpoint_type* endpoint = nullptr)
      : fd_{ fd }
      , buffer_{ buffer }
      , endpoint_{ endpoint }
    {}

    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        iovec iov{ .iov_base = buffer_.data(), .iov_len = buffer_.size() };
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_name = endpoint_ ? endpoint_->data() : nullptr;
        msg.msg_namelen = endpoint_ ? endpoint_->capacity() : 0;

        ::io_uring_prep_recvmsg(sqe, fd_, &msg, 0);
    }

    auto value() noexcept -> std::size_t
    {
        // result < 0 时，父类会优先处理，只有 result >= 0 时才会调用 value() 获取实际结果。
        return static_cast<std::size_t>(this->result);
    }
};

} // namespace net

#endif // BLOG_NET_RECEIVE_MESSAGE_AWAITER_H