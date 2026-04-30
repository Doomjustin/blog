#ifndef BLOG_ASYNC_RECEIVE_STREAM_H
#define BLOG_ASYNC_RECEIVE_STREAM_H

#include <coroutine>
#include <cstdint>
#include <deque>
#include <expected>
#include <system_error>

#include <liburing.h>
#include <liburing/io_uring.h>

#include "io_context.h"
#include "pooled_buffer.h"

namespace async {

class ReceiveStream {
public:
    using result_type = std::expected<PooledBuffer, std::error_code>;

    class NextAwaiter {
    public:
        using resume_type = PooledBuffer;

        explicit NextAwaiter(ReceiveStream& stream)
          : stream_{ stream }
        {}

        [[nodiscard]]
        auto await_ready() const noexcept -> bool;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        auto await_resume() -> std::expected<resume_type, std::error_code>;

    private:
        ReceiveStream& stream_;
    };


    ReceiveStream(IOContext& context, int fd, unsigned bgid);

    ReceiveStream(const ReceiveStream&) = delete;
    auto operator=(const ReceiveStream&) -> ReceiveStream& = delete;

    ReceiveStream(ReceiveStream&& other) noexcept;
    auto operator=(ReceiveStream&& other) noexcept -> ReceiveStream&;

    ~ReceiveStream();

    auto next() -> NextAwaiter
    {
        return NextAwaiter{ *this };
    }

private:
    class MutishotReceiveOperation;

    IOContext* context_;
    int fd_;
    unsigned bgid_;
    MutishotReceiveOperation* operation_{ nullptr };
    bool operation_armed_{ false };

    std::coroutine_handle<> handle_{ nullptr };
    std::deque<result_type> ready_results_;

    void arm_operation();

    void destroy() noexcept;

    void handle_cqe(int result, std::uint32_t flags) noexcept;
};

} // namespace async

#endif // BLOG_ASYNC_RECEIVE_STREAM_H