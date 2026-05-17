#ifndef BLOG_NET_ACCEPTOR_AWAITER_H
#define BLOG_NET_ACCEPTOR_AWAITER_H

#include <async/async.h>

namespace net {

template<typename Protocol>
class AcceptAwaiter : public async::IOAwaiter<AcceptAwaiter<Protocol>, typename Protocol::socket> {
public:
    using endpoint_type = typename Protocol::endpoint;
    using socket_type = typename Protocol::socket;

private:
    int fd_;
    endpoint_type* endpoint_;
    socklen_t addrlen_{};

public:
    AcceptAwaiter(int fd, endpoint_type* endpoint = nullptr) noexcept
      : fd_{ fd }
      , endpoint_{ endpoint }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_accept(sqe, fd_, endpoint_ ? endpoint_->data() : nullptr,
                               endpoint_ ? &addrlen_ : nullptr, 0);
    }

    auto value() noexcept -> socket_type
    {
        if (endpoint_)
            endpoint_->resize(addrlen_);

        return socket_type{ this->result };
    }
};

} // namespace net

#endif // BLOG_NET_ACCEPTOR_AWAITER_H