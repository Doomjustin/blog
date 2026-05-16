#ifndef BLOG_ASYNC_OPERATION_H
#define BLOG_ASYNC_OPERATION_H

#include <coroutine>
#include <cstdint>
#include <utility>

#include <common/common.h>
#include <common/mpsc_queue.h>

namespace async {

/**
 * @brief Base type for all io_uring completion callbacks.
 *
 * Each async I/O request stores a pointer to its owning `Operation` in the
 * SQE user-data field. When the corresponding CQE arrives, the event loop
 * calls `complete` to deliver the result and resume the waiting coroutine.
 */
struct Operation: public MPSCQueueNode {
    Operation* prev{ nullptr };
    Operation* next{ nullptr };
    int scheduled_result_{ 0 }; // result carried when op is routed via submit/post
    bool is_canceling_{ false };
    
    Operation() = default;
    
    Operation(Operation&&) = default;
    auto operator=(Operation&&) -> Operation& = default;

    virtual ~Operation() = default;

    /**
     * @brief Deliver io_uring completion result to the owning coroutine.
     *
     * @param res Completion result. Negative values represent negated `errno`.
     * @param flags CQE flags from io_uring.
     */
    virtual void complete(int res, std::uint32_t flags) = 0;
};


/**
 * @brief Base type for operations that can be wrapped by a timeout combinator.
 *
 * The `parent` pointer enables the combinator pattern: when a
 * `TimeoutCombinator` wraps an inner operation, it sets `parent` to itself
 * so that inner-operation completions are routed through the combinator
 * rather than directly resuming the coroutine. This lets the combinator
 * interpose cancellation logic before deciding whether to resume.
 *
 * When not wrapped, `parent` is null and `resume()` falls back to resuming
 * the coroutine handle directly, matching the behavior of plain `Operation`.
 */
struct CancelableOperation : public Operation {
    /**
     * @brief Pointer to the enclosing combinator, or `nullptr` when standalone.
     *
     * Set by the combinator immediately after construction. Must not be
     * modified after the operation has been submitted to the ring.
     */
    Operation* parent{ nullptr };
    
    CancelableOperation() = default;

    CancelableOperation(CancelableOperation&&) = default;
    auto operator=(CancelableOperation&&) -> CancelableOperation& = default;

    virtual ~CancelableOperation() = default;

    /**
     * @brief Request cancellation for this operation.
     *
     * Leaf awaiters should forward to `IOContext::cancel(this)`. Combinators
     * should recursively cancel all in-flight children.
     */
    virtual void cancel() noexcept = 0;

    /**
     * @brief Route a completion event to the parent combinator or resume directly.
     *
     * Called by derived classes from `complete()` instead of resuming the
     * coroutine handle directly. If a parent combinator is registered, it
     * receives the result; otherwise the coroutine is resumed immediately.
     *
     * @param handle Coroutine handle to resume when no parent is set.
     * @param result Raw CQE result value.
     * @param flags  CQE flags.
     */
    void resume(std::coroutine_handle<> handle, int result, std::uint32_t flags) noexcept
    {
        if (parent) {
            parent->complete(result, flags);
        }
        else if (handle) {
            auto h = std::exchange(handle, nullptr);
            h.resume();
        }
    }
};

/**
 * @brief Constrain operations that support mid-flight cancellation via a parent combinator.
 *
 * A cancelable operation must:
 * - Expose a `resume_type` result alias.
 * - Return an lvalue reference from `context()`.
 * - Accept `await_suspend(handle)` so combinators such as `WhenAnyAwaiter` and
 *   `WhenAllAwaiter` can drive it.
 * - Derive from `CancelableOperation` so the `parent` pointer mechanism is available.
 */
template<typename T>
concept cancelable_operation = requires(T& t)
{
    typename T::resume_type;

    requires std::is_lvalue_reference_v<decltype(t.context())>;
    { t.await_suspend(std::coroutine_handle<>()) } -> std::same_as<bool>;
    t.cancel();
};

} // namespace async

#endif // BLOG_ASYNC_OPERATION_H