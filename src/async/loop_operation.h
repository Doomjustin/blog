#ifndef BLOG_ASYNC_LOOP_OPERATION_H
#define BLOG_ASYNC_LOOP_OPERATION_H

#include <cstddef>
#include <expected>

#include <exceptions.h>
#include <io_context.h>
#include <operation.h>

namespace async {

/**
 * @brief CRTP base for looping io_uring awaiters.
 *
 * Handles awaiters that repeatedly submit SQEs until a full-buffer goal is
 * met (e.g. send-all, receive-all). The base owns the coroutine handle,
 * byte counter, error code, and the span that shrinks with each partial
 * completion. The derived class supplies only `arm()`.
 *
 * @tparam Derived   Concrete awaiter type (CRTP parameter).
 * @tparam SpanType  `std::span<std::byte>` or `std::span<const std::byte>`.
 *
 * ## Contract for Derived
 *
 * - `bool arm() noexcept` — obtain an SQE, call the appropriate
 *   `io_uring_prep_*`, set the SQE user-data to `this`, call
 *   `context_.track(this)`, and return `true`. If the SQ is full, set
 *   `error_code_ = EAGAIN` and return `false`.
 *
 * `set_result()` and `complete()` are provided by the base. Derived may
 * override `complete()` when the io_uring operation requires handling more
 * than one CQE per submission (e.g. zero-copy send).
 */
template<typename Derived, typename SpanType>
class LoopOperation: public CancelableOperation {
public:
    using resume_type = std::size_t;
    using context_type = IOContext;

    /**
     * @brief Construct with the I/O context and the full buffer to process.
     *
     * @param context I/O context whose SQ will receive the SQEs.
     * @param buffer  Span covering the entire data to send or receive.
     */
    LoopOperation(context_type& context, SpanType buffer)
      : context_{ &context }
      , buffer_{ buffer }
    {}

    LoopOperation(LoopOperation&&) = default;
    auto operator=(LoopOperation&&) -> LoopOperation& = default;

    ~LoopOperation() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /**
     * @brief Arm the first SQE and suspend the coroutine.
     *
     * Returns `false` (skip suspension) when the SQ is full; `error_code_`
     * is set to `EAGAIN` so `await_resume` reports the error.
     */
    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        return static_cast<Derived*>(this)->arm();
    }

    /**
     * @brief Return total bytes processed, or an error code.
     */
    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        return bytes_processed_;
    }

    /**
     * @brief Called by the io_uring event loop when a CQE arrives.
     *
     * Updates `bytes_processed_` and advances the buffer slice, then either
     * resumes the coroutine (on completion or error) or re-arms the next SQE.
     */
    void complete(int result, std::uint32_t flags) noexcept override
    {
        context_->untrack(this);
        set_result(result, flags);
        finish_or_rearm(result, flags);
    }

    auto context() noexcept -> context_type&
    {
        return *context_;
    }

protected:
    /**
     * @brief Record a partial CQE result.
     *
     * - Positive result: advances the buffer slice and accumulates the count.
     * - Zero result:     peer closed the connection; sets `ECONNABORTED`.
     * - Negative result: maps the kernel error into `error_code_`.
     */
    void set_result(int result, std::uint32_t /*flags*/) noexcept
    {
        if (result > 0) {
            bytes_processed_ += static_cast<std::size_t>(result);
            buffer_ = buffer_.subspan(static_cast<std::size_t>(result));
        }
        else if (result == 0)
            error_code_ = ECONNABORTED;
        else
            error_code_ = -result;
    }

    /**
     * @brief Resume the coroutine or re-arm the next SQE.
     *
     * Centralizes the retry decision so `complete()` overrides (e.g. the
     * zero-copy variant) can call it after handling their extra CQEs.
     *
     * Resumes if: cancellation is pending, an error occurred, or the buffer
     * has been fully consumed. Otherwise calls `Derived::arm()`; if the SQ
     * is full at that point, resumes immediately with an EAGAIN error.
     */
    void finish_or_rearm(int result, std::uint32_t flags) noexcept
    {
        if (is_canceling_ || error_code_ != 0 || buffer_.empty()) {
            if (is_canceling_ && error_code_ == 0)
                error_code_ = ECANCELED;

            resume(handle_, result, flags);
        }
        else if (!static_cast<Derived*>(this)->arm()) {
            resume(handle_, 0, 0);
        }
    }

    context_type* context_;
    SpanType buffer_;
    std::coroutine_handle<> handle_{ nullptr };
    std::size_t bytes_processed_{ 0 };
    int error_code_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_LOOP_OPERATION_H
