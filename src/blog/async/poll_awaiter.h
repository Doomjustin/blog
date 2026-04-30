#ifndef BLOG_ASYNC_POLL_AWAITER_H
#define BLOG_ASYNC_POLL_AWAITER_H

#include <coroutine>
#include <expected>
#include <system_error>

#include <liburing.h>

#include "io_context.h"
#include "operation.h"

namespace async {

/**
 * @brief Suspend until a file descriptor becomes ready for the requested events.
 *
 * Submits an `io_uring_prep_poll_add` SQE and resumes the calling coroutine
 * when the CQE arrives. Useful for waiting on file descriptors that do not
 * have a dedicated io_uring opcode (e.g. `signalfd`, `eventfd`).
 */
class PollAwaiter: public Operation {
public:
    using resume_type = void;

    /**
     * @brief Construct with the fd and event mask to poll.
     *
     * @param context I/O context that drives this operation.
     * @param fd      File descriptor to watch.
     * @param events  `POLLIN`/`POLLOUT`/... mask forwarded to `io_uring_prep_poll_add`.
     */
    PollAwaiter(IOContext& context, int fd, short events)
      : context_{ context }, 
        fd_{ fd }, 
        events_{ events }
    {}

    ~PollAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> handle) -> void;

    auto await_resume() -> std::expected<void, std::error_code>;

    void prepare(::io_uring_sqe* sqe) noexcept;

    void set_result(int result, [[maybe_unused]] std::uint32_t flags) noexcept;

    void complete(int res, std::uint32_t flags) noexcept override;

    auto context() noexcept -> IOContext& { return context_; }

private:
    IOContext& context_;
    int fd_;
    short events_;

    std::coroutine_handle<> handle_{ nullptr };
    int error_code_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_POLL_AWAITER_H