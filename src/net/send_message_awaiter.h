#ifndef BLOG_NET_SEND_MESSAGE_AWAITER_H
#define BLOG_NET_SEND_MESSAGE_AWAITER_H

#include <liburing.h>

#include <async/async.h>

namespace net {

/// @brief 基于 io_uring 的发送 awaiter，把只读字节缓冲区发送到 socket fd。
/// @tparam Protocol 协议标签类型，用于与具体 socket/stream 类型配套。
template<typename Protocol>
class SendMessageAwaiter : public async::IOAwaiter<SendMessageAwaiter<Protocol>, std::size_t> {
private:
    int fd_;
    std::span<const std::byte> buffer_;

public:
    /// @brief 构造发送 awaiter。
    /// @param[in] fd 目标 socket 文件描述符。
    /// @param[in] buffer 待发送的只读字节序列。
    SendMessageAwaiter(int fd, std::span<const std::byte> buffer)
      : fd_{ fd }
      , buffer_{ buffer }
    {}

    /// @brief 准备 io_uring SQE，提交 send 操作。
    /// @param[in] sqe 待填充的提交队列项。
    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_send(sqe, fd_, buffer_.data(), buffer_.size(), MSG_NOSIGNAL);
    }

    /// @brief 返回发送成功的字节数。
    /// @return send 完成后的字节数（仅在 result >= 0 时被调用）。
    auto value() noexcept -> std::size_t
    {
        // result < 0 时，父类会优先处理，只有 result >= 0 时才会调用 value() 获取实际结果。
        return static_cast<std::size_t>(this->result);
    }
};

} // namespace net

#endif // BLOG_NET_SEND_MESSAGE_AWAITER_H