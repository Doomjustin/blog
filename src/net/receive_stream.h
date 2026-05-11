#ifndef BLOG_NET_RECEIVE_STREAM_H
#define BLOG_NET_RECEIVE_STREAM_H

#include <cerrno>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <expected>
#include <system_error>

#include <net/pooled_buffer.h>

#include <liburing.h>
#include <liburing/io_uring.h>

#include "async/io_context.h"
#include "async/operation.h"

#include <async/async.h>

namespace net {

/**
 * @brief Streaming receive via `recv_multishot` backed by a registered buffer ring.
 *
 * A single multishot SQE is submitted to io_uring once; the kernel reuses it
 * to deliver multiple completions without requiring the user to resubmit after
 * each packet. Each completion delivers a `PooledBuffer` slice that must be
 * explicitly returned to the ring (handled by `PooledBuffer`'s destructor).
 *
 * Callers iterate over incoming data by `co_await`-ing `next()` in a loop
 * until an error or empty buffer signals end-of-stream.
 */
class ReceiveStream {
public:
    using result_type = std::expected<PooledBuffer, std::error_code>;
    using context_type = async::IOContext;

    /**
     * @brief Awaiter returned by `ReceiveStream::next()`.
     *
     * Suspends the coroutine if no data is currently buffered; resumes
     * immediately when the stream has a pending result queued.
     */
    class NextAwaiter: public async::CancelableOperation {
    public:
        using resume_type = PooledBuffer;

        explicit NextAwaiter(ReceiveStream& stream)
          : stream_{ stream }
        {}

        [[nodiscard]]
        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool;
        auto await_resume() -> std::expected<resume_type, std::error_code>;

        void complete(int result, std::uint32_t flags) noexcept override
        {
            if (result == -ECANCELED) is_canceling_ = true;
            this->resume(handle_, result, flags);
        }

        void cancel() noexcept override
        {
            if (stream_.cancel_operation_ == this) {
                stream_.cancel_operation_ = nullptr;
                is_canceling_ = true;
                
                this->scheduled_result_ = -ECANCELED;
                stream_.context_->submit(this);
            }
        }

        auto context() noexcept -> async::IOContext&
        {
            return *stream_.context_;
        }

    private:
        ReceiveStream& stream_;
        std::coroutine_handle<> handle_{ nullptr };
    };


    /**
     * @brief Construct a stream bound to a specific buffer group.
     *
     * @param context I/O context that drives the multishot receive.
     * @param fd      Connected socket file descriptor.
     * @param bgid    Buffer group ID registered with `setup_buffer_ring()`.
     */
    ReceiveStream(context_type& context, int fd, unsigned bgid);

    ReceiveStream(const ReceiveStream&) = delete;
    auto operator=(const ReceiveStream&) -> ReceiveStream& = delete;

    ReceiveStream(ReceiveStream&& other) noexcept;
    auto operator=(ReceiveStream&& other) noexcept -> ReceiveStream&;

    ~ReceiveStream();

    /**
     * @brief Obtain the next data buffer from the stream.
     *
     * Returns immediately if a result is already queued; otherwise suspends
     * until the kernel delivers the next multishot CQE.
     *
     * @return Awaiter that resolves to a `PooledBuffer` on success or an
     *         error code on failure.
     */
    auto next() -> NextAwaiter
    {
        return NextAwaiter{ *this };
    }

private:
    class MutishotReceiveOperation;

    context_type* context_;
    int fd_;
    unsigned bgid_;
    MutishotReceiveOperation* operation_{ nullptr };
    bool operation_armed_{ false };

    // std::coroutine_handle<> handle_{ nullptr };
    async::CancelableOperation* cancel_operation_{ nullptr };
    std::deque<result_type> ready_results_;

    auto arm_operation() -> bool;

    void destroy() noexcept;

    void handle_cqe(int result, std::uint32_t flags) noexcept;
};

} // namespace net

#endif // BLOG_NET_RECEIVE_STREAM_H