#ifndef BLOG_ASYNC_WHEN_ANY_AWAITER_H
#define BLOG_ASYNC_WHEN_ANY_AWAITER_H

#include <array>
#include <coroutine>
#include <cstddef>
#include <expected>
#include <system_error>
#include <utility>
#include <variant>

#include <operation.h>

namespace async {

namespace detail {

template<typename First, typename... Rest>
inline constexpr bool all_same_v = (std::is_same_v<First, Rest> && ...);

} // namespace detail

/**
 * @brief Suspend until the FIRST inner operation completes, cancelling the rest.
 *
 * Submits N operations concurrently. When the first CQE arrives, the winning
 * index is recorded and the remaining N-1 operations are cancelled via
 * `IOContext::cancel()`. The coroutine resumes after ALL N CQEs have been
 * collected (including the cancellation results), ensuring no in-flight SQE
 * is abandoned.
 *
 * @tparam Awaiters `cancelable_operation` types to race.
 *
 * ## Return value
 *
 * - If all inner operations share the same `resume_type R`:
 *   `std::expected<R, std::error_code>` — the winner's result directly.
 * - Otherwise:
 *   `std::variant<std::expected<R0,ec>, std::expected<R1,ec>, ...>` — the
 *   active index identifies the winner.
 *
 * ## Example
 *
 * ```cpp
 * // heterogeneous: returns variant
 * auto result = co_await when_any(
 *     net::receive(ctx, sock, buf),
 *     async::sleep_for(ctx, 5s));
 * if (result.index() == 0) { // receive won
 * }
 *
 * // homogeneous: returns expected<std::size_t>
 * auto result = co_await when_any(
 *     net::send(ctx, s1, buf),
 *     net::send(ctx, s2, buf));
 * if (!result) { // one of them failed
 * }
 * ```
 *
 * @note All awaiters must share the same `IOContext`.
 * @note The submission queue must have at least N free slots.
 */
template<cancelable_operation... Awaiters>
class WhenAnyAwaiter: public CancelableOperation {
public:
    using resume_type = std::conditional_t<
        detail::all_same_v<typename Awaiters::resume_type...>,
        std::expected<std::tuple_element_t<0, std::tuple<typename Awaiters::resume_type...>>, std::error_code>,
        std::variant<std::expected<typename Awaiters::resume_type, std::error_code>...>
    >;

    explicit WhenAnyAwaiter(Awaiters&&... awaiters)
      : awaiters_{ std::forward<Awaiters>(awaiters)... }
    {
        setup_slots(std::index_sequence_for<Awaiters...>{});
    }

    ~WhenAnyAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    /**
     * @brief Wire slot parents, then submit all SQEs.
     *
     * Each inner operation's `parent` is set to its corresponding `Slot`.
     * When a CQE arrives, the inner op's `CancelableOperation::resume()`
     * routes the result to `Slot::complete()`, which calls back into
     * `WhenAnyAwaiter::on_slot_complete()` with the index.
     */
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;
        set_parents(std::index_sequence_for<Awaiters...>{});
        arm_all(std::index_sequence_for<Awaiters...>{});
    }

    /**
     * @brief Return the winning operation's result wrapped in the variant.
     *
     * The active variant index equals the winner index recorded when the
     * first CQE arrived.
     */
    auto await_resume() -> resume_type
    {
        if constexpr (detail::all_same_v<typename Awaiters::resume_type...>)
            return get_winner_result(std::index_sequence_for<Awaiters...>{});
        else
            return collect_winner(std::index_sequence_for<Awaiters...>{});
    }

    /**
     * @brief Unused; completions are routed through `Slot::complete()` instead.
     *
     * `WhenAnyAwaiter` itself is never set as the `parent` of any inner op;
     * the per-index `Slot` objects serve that role.
     */
    void complete(int /*result*/, std::uint32_t /*flags*/) noexcept override {}

    auto context() noexcept -> decltype(auto)
    {
        return std::get<0>(awaiters_).context();
    }

private:
    /**
     * @brief Per-operation proxy that carries the awaiter index.
     *
     * Set as the `parent` of each inner operation. When the inner op calls
     * `CancelableOperation::resume()`, it reaches `Slot::complete()`, which
     * forwards the result and its own index to `WhenAnyAwaiter`.
     */
    struct Slot: public CancelableOperation {
        WhenAnyAwaiter* owner{ nullptr };
        std::size_t index{ 0 };

        void complete(int result, std::uint32_t flags) noexcept override
        {
            owner->on_slot_complete(index, result, flags);
        }
    };

    std::tuple<Awaiters...> awaiters_;
    std::array<Slot, sizeof...(Awaiters)> slots_{};
    std::coroutine_handle<> handle_;
    int pending_{ static_cast<int>(sizeof...(Awaiters)) };
    int winner_{ -1 };

    template<std::size_t... Is>
    void setup_slots(std::index_sequence<Is...> /*index*/) noexcept
    {
        (..., (slots_[Is].owner = this, slots_[Is].index = Is));
    }

    template<std::size_t... Is>
    void set_parents(std::index_sequence<Is...> /*index*/) noexcept
    {
        (..., (std::get<Is>(awaiters_).parent = &slots_[Is]));
    }

    template<std::size_t... Is>
    void arm_all(std::index_sequence<Is...> /*index*/) noexcept
    {
        (..., (void)std::get<Is>(awaiters_).await_suspend(handle_));
    }

    void on_slot_complete(std::size_t index, int result, std::uint32_t flags) noexcept
    {
        if (winner_ < 0) {
            // First completion: record winner and cancel the other N-1 SQEs.
            // Cancellation CQEs have null user-data so the event loop discards
            // them; they do not affect the pending_ counter.
            winner_ = static_cast<int>(index);
            cancel_losers(index, std::index_sequence_for<Awaiters...>{});
        }

        if (--pending_ == 0)
            this->resume(handle_, result, flags);
    }

    template<std::size_t... Is>
    void cancel_losers(std::size_t winner, std::index_sequence<Is...> /*index*/) noexcept
    {
        (..., (void)(Is != winner && (context().cancel(&std::get<Is>(awaiters_)), true)));
    }

    // Homogeneous path: all resume_types identical, return expected<R> directly.
    template<std::size_t I, std::size_t... Is>
    auto get_winner_result(std::index_sequence<I, Is...> /*index*/) -> resume_type
    {
        if (static_cast<std::size_t>(winner_) == I)
            return std::get<I>(awaiters_).await_resume();

        if constexpr (sizeof...(Is) > 0)
            return get_winner_result(std::index_sequence<Is...>{});

        // unreachable
        return std::get<0>(awaiters_).await_resume();
    }

    // Heterogeneous path: return variant with active index = winner.
    using variant_type = std::variant<std::expected<typename Awaiters::resume_type, std::error_code>...>;

    template<std::size_t I, std::size_t... Is>
    auto collect_winner(std::index_sequence<I, Is...> /*index*/) -> variant_type
    {
        if (static_cast<std::size_t>(winner_) == I)
            return variant_type{ std::in_place_index<I>, std::get<I>(awaiters_).await_resume() };

        if constexpr (sizeof...(Is) > 0)
            return collect_winner(std::index_sequence<Is...>{});

        // unreachable: winner_ is always a valid index
        return variant_type{ std::in_place_index<0>, std::get<0>(awaiters_).await_resume() };
    }
};


/**
 * @brief Factory for `WhenAnyAwaiter`; deduces awaiter types from arguments.
 *
 * @param awaiters Inner operations to race; moved into the awaiter.
 */
template<cancelable_operation... Awaiters>
auto when_any(Awaiters&&... awaiters)
{
    return WhenAnyAwaiter<std::remove_cvref_t<Awaiters>...>{ std::forward<Awaiters>(awaiters)... };
}

} // namespace async

#endif // BLOG_ASYNC_WHEN_ANY_AWAITER_H
