#ifndef BLOG_NET_CONNECT_AWAITER_H
#define BLOG_NET_CONNECT_AWAITER_H

#include <liburing.h>

#include <async/async.h>

namespace net {

/// @brief 用于异步 connect 操作的 awaiter，封装 fd 与目标 endpoint。
template<typename Protocol>
class ConnectAwaiter : public async::IOAwaiter<ConnectAwaiter<Protocol>> {
private:
    int fd_;
    typename Protocol::endpoint endpoint_;

public:
    /// @brief 构造一个 ConnectAwaiter。
    /// @param[in] fd 用于发起 connect 的 socket 文件描述符。
    /// @param[in] endpoint 目标 endpoint，类型由 Protocol 决定。
    ConnectAwaiter(int fd, const typename Protocol::endpoint& endpoint)
      : fd_{ fd }
      , endpoint_{ endpoint }
    {}

    /// @brief 向 io_uring SQE 写入 connect 请求参数。
    /// @param[in,out] sqe 待填充的 io_uring submission queue entry。
    /// @return 无返回值。
    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_connect(sqe, fd_, endpoint_.data(), endpoint_.size());
    }
};

} // namespace net

#endif // BLOG_NET_CONNECT_AWAITER_H