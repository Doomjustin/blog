#ifndef BLOG_NET_SEND_AWAITER_H
#define BLOG_NET_SEND_AWAITER_H

#include <liburing.h>

#include <async/async.h>

namespace net {

/// @brief 基于 io_uring 的 send 协程 Awaiter。
///
/// 封装单次非阻塞 send 操作，支持协程挂起与恢复。
/// 用于向指定 fd 发送 buffer 数据，完成后返回实际发送字节数。
///
/// ```cpp
/// co_await SendAwaiter(fd, std::as_bytes(std::span(data)));
/// ```
class SendAwaiter : public async::IOAwaiter<SendAwaiter, std::size_t> {
private:
    int fd_;
    std::span<const std::byte> buffer_;

public:
    /// @brief 构造 send awaiter。
    /// @param[in] fd 目标文件描述符（socket）。
    /// @param[in] buffer 待发送数据缓冲区。
    SendAwaiter(int fd, std::span<const std::byte> buffer)
      : fd_{ fd }
      , buffer_{ buffer }
    {}

    /// @brief 填充 io_uring SQE，准备 send 操作。
    /// @param[in] sqe io_uring 提交队列项。
    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_send(sqe, fd_, buffer_.data(), buffer_.size(), MSG_NOSIGNAL);
    }
};

} // namespace net

#endif // BLOG_NET_SEND_AWAITER_H