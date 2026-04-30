#ifndef BLOG_ASYNC_WRITE_SEQUENCE_AWAITER_H
#define BLOG_ASYNC_WRITE_SEQUENCE_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>
#include <system_error>

#include <liburing.h>

#include "common/operations.h"
#include "io_context.h"
#include "operation.h"

namespace async {

/**
 * @brief Awaiter that performs a single gather-write (`writev`) through io_uring.
 *
 * This awaiter copies only iovec descriptors (not payload bytes) from the
 * caller-provided buffer sequence, submits one writev-style request, and
 * resumes the suspended coroutine when completion arrives.
 *
 * @tparam Buffer  Buffer sequence type satisfying `sequence_buffer`.
 */
template<sequence_buffer Buffer>
class WriteSequenceAwaiter: public Operation {
public:
    using resume_type = std::size_t;
    using context_type = IOContext;

    /**
     * @brief Build iovec metadata from a sequence of immutable buffers.
     *
     * @param context Context used to submit and complete the operation.
     * @param socket  Connected socket file descriptor.
     * @param buffers Source buffer sequence written in order.
     * @pre All underlying buffer memory referenced by `buffers` must remain
     *      valid until the coroutine resumes.
     */
    WriteSequenceAwaiter(context_type& context, int socket, const Buffer& buffers)
      : context_(context), socket_(socket)
    {
        iov_.reserve(std::ranges::size(buffers));

        for (const auto& chunk : buffers) {
            auto data = buffer(chunk);

            iov_.push_back(::iovec{
                .iov_base = const_cast<std::byte*>(data.data()),
                .iov_len = data.size()
            });
        }
    }

    ~WriteSequenceAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /** @brief Submit the io_uring request and suspend the current coroutine. */
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;

        auto* sqe = context_.sqe();

        prepare(sqe);
        ::io_uring_sqe_set_data(sqe, this);
    }

    /**
     * @brief Return completion result as bytes written or an error code.
     *
     * @return `std::expected<std::size_t, std::error_code>` containing bytes
     *         written on success, otherwise the kernel error from CQE result.
     */
    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return std::unexpected{ std::error_code{ error_code_, std::generic_category() } };

        return bytes_written_;
    }

    /** @brief Fill SQE as a writev request using cached iovec descriptors. */
    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_writev(sqe, socket_, iov_.data(), iov_.size(), 0);
    }

    /** @brief Store CQE result in success/error fields for later resume handling. */
    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result >= 0)
            bytes_written_ = static_cast<std::size_t>(result);
        else
            error_code_ = -result;
    }

    /** @brief Complete callback from event loop; records result and resumes waiter. */
    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

    /** @brief Access the associated execution context. */
    auto context() noexcept -> context_type& { return context_; }

private:
    std::coroutine_handle<> handle_{ nullptr };
    context_type& context_;
    int socket_;

    std::vector<::iovec> iov_;
    std::size_t bytes_written_{ 0 };
    int error_code_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_WRITE_SEQUENCE_AWAITER_H