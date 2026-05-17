#ifndef BLOG_NET_RECEIVE_AWAITER_H
#define BLOG_NET_RECEIVE_AWAITER_H

#include <liburing.h>

#include <async/async.h>

namespace net {

/// @brief 基于 io_uring 的 receive 协程 Awaiter。
///
/// 封装单次非阻塞 recv 操作，支持协程挂起与恢复。
/// 用于从指定 fd 接收数据到 buffer，完成后返回实际接收字节数。
///
/// ```c++
/// co_await ReceiveAwaiter(fd, std::as_writable_bytes(std::span(data)));
/// ```
class ReceiveAwaiter : public async::IOAwaiter<ReceiveAwaiter, std::size_t> {
private:
    int fd_;
    std::span<std::byte> buffer_;

public:
    /// @brief 构造 receive awaiter。
    /// @param[in] fd 目标文件描述符（socket）。
    /// @param[in] buffer 接收数据缓冲区。
    ReceiveAwaiter(int fd, std::span<std::byte> buffer)
      : fd_{ fd }
      , buffer_{ buffer }
    {}

    /// @brief 填充 io_uring SQE，准备 recv 操作。
    /// @param[in] sqe io_uring 提交队列项。
    /// @return 无返回值。
    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_recv(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }
};

} // namespace net

#endif // BLOG_NET_RECEIVE_AWAITER_H