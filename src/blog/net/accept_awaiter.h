#ifndef BLOG_NET_ACCEPT_AWAITER_H
#define BLOG_NET_ACCEPT_AWAITER_H

#include <coroutine>
#include <expected>
#include <utility>

#include <liburing.h>

#include "async/io_context.h"
#include "async/operation.h"
#include "common/exceptions.h"

namespace net {

/**
 * @brief Suspend until an incoming connection is accepted via io_uring.
 *
 * Submits one `io_uring_prep_accept` SQE and resumes the coroutine with
 * a fully constructed socket object wrapping the accepted fd, or an error
 * code on failure.
 *
 * @tparam Protocol Protocol type defining `socket` and `endpoint` associated types.
 */
template<typename Protocol>
class AcceptAwaiter: public async::Operation {
public:
    using socket_type = typename Protocol::socket;
    using endpoint_type = typename Protocol::endpoint;
    using context_type = async::IOContext;
    using resume_type = socket_type;

    /**
     * @brief Construct for accept without peer capture.
     *
     * @param context I/O context that drives this operation.
     * @param fd      Listening socket file descriptor.
     * @param peer    Optional endpoint buffer to capture the peer address.
     *                Pass `nullptr` to discard peer information.
     * @pre If non-null, `*peer` must remain valid until the coroutine is resumed.
     */
    AcceptAwaiter(context_type& context, int fd, endpoint_type* peer = nullptr)
      : context_{ context }, fd_{ fd }, peer_{ peer }
    {
        if (peer_)
            addrlen_ = peer_->capacity();
    } 

    ~AcceptAwaiter() = default;

    [[nodiscard]] 
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;

        auto* sqe = context_.sqe();
        
        prepare(sqe);
        ::io_uring_sqe_set_data(sqe, this);
    }

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        return resume_type{ result_fd_, context_ };
    }

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_accept(sqe, 
                               fd_, 
                               peer_ ? peer_->data() : nullptr, 
                               peer_ ? &addrlen_ : nullptr, 
                               0
                        );
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result >= 0)
            result_fd_ = result;
        else
            error_code_ = -result;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (peer_ && result >= 0)
            peer_->resize(addrlen_);

        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

    auto context() noexcept -> context_type& { return context_; }

private:
    context_type& context_;
    int fd_;
    endpoint_type* peer_;
    socklen_t addrlen_{};

    std::coroutine_handle<> handle_{ nullptr };
    int result_fd_{ -1 };
    int error_code_{ 0 };
};

} // namespace net

#endif // BLOG_NET_ACCEPT_AWAITER_H