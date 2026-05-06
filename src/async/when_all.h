#ifndef BLOG_ASYNC_WHEN_ALL_AWAITER_H
#define BLOG_ASYNC_WHEN_ALL_AWAITER_H

#include <array>
#include <coroutine>
#include <cstddef>
#include <expected>
#include <system_error>
#include <tuple>
#include <utility>

#include <operation.h>

namespace async {

/**
 * @brief Suspend until ALL inner operations complete, collecting every result.
 *
 * Submits all N inner operations concurrently by calling each one's
 * `await_suspend`. The coroutine resumes only after every CQE has been
 * received. Each inner operation's result is preserved independently, so
 * partial failures do not cancel the remaining operations.
 *
 * @tparam Awaiters `cancelable_operation` types to run in parallel.
 *
 * ## Return value
 *
 * `std::tuple<std::expected<R0, ec>, std::expected<R1, ec>, ...>` in
 * argument order. Check each element individually for errors.
 *
 * ## Example
 *
 * ```cpp
 * auto [r_read, r_write] = co_await when_all(
 *     async::read(ctx, fd_in, recv_buf),
 *     async::write(ctx, fd_out, send_buf));
 * ```
 *
 * @note All awaiters must share the same `IOContext`.
 * @note The submission queue must have at least N free slots.
 */
template<cancelable_operation... Awaiters>
class WhenAllAwaiter: public CancelableOperation {
public:
    using resume_type = std::tuple<std::expected<typename Awaiters::resume_type, std::error_code>...>;

    explicit WhenAllAwaiter(Awaiters&&... awaiters)
      : awaiters_{ std::forward<Awaiters>(awaiters)... }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /**
     * @brief Set each inner operation's parent to `this` and submit all SQEs.
     *
     * Completions are routed through `CancelableOperation::resume()` to
     * `WhenAllAwaiter::complete()`, which counts down until all N CQEs arrive.
     */
    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        setup_parents(std::index_sequence_for<Awaiters...>{});
        return arm_all(std::index_sequence_for<Awaiters...>{});
    }

    /**
     * @brief Return a tuple of each inner operation's result.
     *
     * Each element is the `std::expected` produced by the corresponding
     * inner awaiter's own `await_resume()`.
     */
    auto await_resume() -> resume_type
    {
        return collect(std::index_sequence_for<Awaiters...>{});
    }

    /**
     * @brief Called by each inner operation's completion via the parent mechanism.
     *
     * Resumes the coroutine once all N completions have been received.
     */
    void complete(int result, std::uint32_t flags) noexcept override
    {
        if (--pending_ == 0)
            this->resume(handle_, result, flags);
    }

    void cancel() noexcept override
    {
        cancel_all(std::index_sequence_for<Awaiters...>{});
    }

    auto context() noexcept -> decltype(auto)
    {
        return std::get<0>(awaiters_).context();
    }

private:
    std::tuple<Awaiters...> awaiters_;
    std::array<bool, sizeof...(Awaiters)> armed_{};
    std::coroutine_handle<> handle_;
    int pending_{ static_cast<int>(sizeof...(Awaiters)) };

    template<std::size_t... Is>
    void setup_parents(std::index_sequence<Is...> /*index*/) noexcept
    {
        (..., (std::get<Is>(awaiters_).parent = this));
    }

    template<std::size_t... Is>
    auto arm_all(std::index_sequence<Is...> /*index*/) noexcept -> bool
    {
        bool any_armed = false;
        (..., arm_one<Is>(any_armed));
        return any_armed;
    }

    template<std::size_t I>
    void arm_one(bool& any_armed) noexcept
    {
        auto& awaiter = std::get<I>(awaiters_);
        if (awaiter.await_ready()) {
            --pending_;
            return;
        }

        if (awaiter.await_suspend(handle_)) {
            armed_[I] = true;
            any_armed = true;
            return;
        }

        --pending_;
    }

    template<std::size_t... Is>
    auto collect(std::index_sequence<Is...> /*index*/) -> resume_type
    {
        return resume_type{ std::get<Is>(awaiters_).await_resume()... };
    }

    template<std::size_t... Is>
    void cancel_all(std::index_sequence<Is...> /*index*/) noexcept
    {
        (..., (void)(armed_[Is] && (std::get<Is>(awaiters_).cancel(), true)));
    }
};


/**
 * @brief Factory for `WhenAllAwaiter`; deduces awaiter types from arguments.
 *
 * @param awaiters Inner operations to run concurrently; moved into the awaiter.
 */
template<cancelable_operation... Awaiters>
auto when_all(Awaiters&&... awaiters)
{
    return WhenAllAwaiter<std::remove_cvref_t<Awaiters>...>{
        std::forward<Awaiters>(awaiters)...
    };
}

} // namespace async

#endif // BLOG_ASYNC_WHEN_ALL_AWAITER_H
