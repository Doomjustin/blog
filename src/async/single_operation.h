#ifndef BLOG_ASYNC_SINGLE_OPERATION_H
#define BLOG_ASYNC_SINGLE_OPERATION_H

#include <expected>

#include <exceptions.h>
#include <io_context.h>
#include <operation.h>

namespace async {

/**
 * @brief CRTP base for single-shot io_uring awaiters.
 *
 * Provides the full coroutine protocol (`await_ready`, `await_suspend`,
 * `await_resume`) and the `complete()` callback for awaiters that submit
 * exactly one SQE and resume once the corresponding CQE arrives.
 *
 * @tparam Derived     Concrete awaiter type (CRTP parameter).
 * @tparam ResumeType  Type returned by `await_resume()` on success, or
 *                     `void` for operations that carry no result value.
 *
 * ## Contract for Derived
 *
 * - `void prepare(::io_uring_sqe*) noexcept` — fill in the SQE fields.
 * - `void set_result(int result, std::uint32_t flags) noexcept` — store the
 *   successful CQE result. Called only when `result >= 0` and
 *   `ResumeType != void`.
 * - `ResumeType result() noexcept` — return the stored result to the
 *   coroutine. Required only when `ResumeType != void`.
 *
 * If `await_suspend` cannot obtain an SQE (SQ full), it sets
 * `error_code_ = EAGAIN` and returns `false`, skipping suspension.
 */
template<typename Derived, typename ResumeType>
class SingleOperation: public CancelableOperation {
public:
    using is_single_shot = void;
    using resume_type = ResumeType;
    using context_type = IOContext;

    /**
     * @brief Construct with the I/O context that drives this operation.
     *
     * @param context I/O context whose SQ will receive the SQE.
     */
    SingleOperation(context_type& context)
      : context_{ context }
    {}

    ~SingleOperation() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /**
     * @brief Submit the SQE and suspend the coroutine.
     *
     * Returns `false` (skip suspension) if the SQ is full; `error_code_`
     * is set to `EAGAIN` in that case so `await_resume` reports the error.
     */
    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        this->handle_ = handle;

        if (auto* sqe = context_.sqe()) {
            static_cast<Derived*>(this)->prepare(sqe);
            ::io_uring_sqe_set_data(sqe, this);

            context_.track(this);
            return true;
        }

        error_code_ = EAGAIN;
        return false;
    }

    /**
     * @brief Return the operation result to the coroutine.
     *
     * Returns an error if `error_code_` is set, otherwise calls
     * `Derived::result()`. For `void` operations, returns an empty
     * `std::expected<void, ...>` on success.
     */
    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        if constexpr (std::is_void_v<resume_type>)
            return {};
        else
            return static_cast<Derived*>(this)->result();
    }

    /**
     * @brief Called by the io_uring event loop when the CQE arrives.
     *
     * Untracks the operation, sets `error_code_` for negative results, and
     * calls `Derived::set_result()` for non-void successful results before
     * resuming the coroutine.
     */
    void complete(int result, std::uint32_t flags) noexcept override
    {
        context_.untrack(this);

        if (result < 0)
            error_code_ = -result;
        else if constexpr (!std::is_void_v<resume_type>)
            static_cast<Derived*>(this)->set_result(result, flags);

        this->resume(handle_, result, flags);
    }

    auto context() noexcept -> context_type&
    {
        return context_;
    }

protected:
    context_type& context_;
    std::coroutine_handle<> handle_{ nullptr };
    int error_code_{ 0 };
};


/**
 * @brief Constrain inner operations that can be wrapped with timeout semantics.
 *
 * An operation satisfies this concept by exposing `using is_single_shot = void`
 * inside the class, which is provided automatically by the
 * `SingleOperation<Derived, ResumeType>` CRTP base.
 */
template<typename T>
concept single_shot_only_operation = requires { typename T::is_single_shot; };

} // namespace async

#endif // BLOG_ASYNC_SINGLE_OPERATION_H