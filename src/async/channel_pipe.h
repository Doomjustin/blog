#ifndef BLOG_ASYNC_CHANNEL_PIPE_H
#define BLOG_ASYNC_CHANNEL_PIPE_H

/**
 * @file channel_pipe.h
 * @brief Credit-based cross-thread pipeline channel with zero hot-path allocation.
 *
 * `ChannelSender<T>` and `ChannelReceiver<T>` are a paired, single-producer /
 * single-consumer (SPSC) async channel for pipeline architectures where producer
 * and consumer live on **different** IOContext threads.
 *
 * Design contract (no atomics on hot paths):
 *  - `ChannelSender<T>` and every `co_await sender.send(v)` run exclusively on
 *    the *sender's* IOContext thread.
 *  - `ChannelReceiver<T>` and every `co_await receiver.receive()` run exclusively
 *    on the *receiver's* IOContext thread.
 *  - Cross-thread communication uses only `IOContext::post(Operation*)`, whose
 *    thread-safety guarantee is provided by the MPSC queue inside IOContext.
 *
 * Node-pool protocol (zero hot-path heap allocation):
 *  At construction `capacity` `DataOp<T>` nodes are pre-allocated.
 *  These nodes serve as both the credit tokens *and* the message carriers:
 *  1. Sender's `free_list_` holds available nodes (= available credits).
 *  2. On `send()`: pop a node, set its `value`, post it to receiver (Thread B).
 *  3. Receiver's `on_data_arrived()`: if a coroutine is waiting, hand off the
 *     value and immediately recycle the node; otherwise push the node (with
 *     its value still inside) onto `data_node_queue_`.
 *  4. Consumer reads via `try_consume()`: pops the node, moves the value out,
 *     then calls `recycle_node()` to put it in the freed chain.
 *  5. `recycle_node()` accumulates freed nodes until `credit_batch` is reached,
 *     then posts the chain head back to the sender (Thread A).
 *  6. Sender's `on_credits_returned()` restores all nodes to `free_list_` and
 *     wakes parked senders in FIFO order.
 *
 * Backpressure invariant:
 *   free_list_.count + data_node_queue_.size() + in-transit nodes == capacity
 *
 *  A node is NOT returned to free_list_ until its value has been consumed by
 *  the receiver, so the sender can never send more than `capacity` unread items.
 *
 * Close semantics:
 *  - Either endpoint may call `close()` (or destroy the handle).
 *  - Sender close posts `SenderCloseOp`; FIFO ordering guarantees all previously
 *    posted `DataOp`s are delivered before the close notification arrives.
 *  - Receiver close posts `ReceiverCloseOp` and fails all parked senders.
 *
 * Cancellation safety:
 *  All `Operation::complete()` implementations check `res == -ECANCELED`.
 *  On cancellation (IOContext teardown), business logic is skipped and any
 *  in-flight state is simply discarded --- the channel is torn down anyway.
 *
 * @note Capacity must be >= 1.  For cross-thread MPMC, use `Channel<T>`.
 */

#include <algorithm>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include <async/io_context.h>
#include <async/operation.h>

namespace async {

// -- Error category ------------------------------------------------------------

enum class ChannelPipeError : std::uint8_t { Closed = 1 };

inline auto channel_pipe_category() noexcept -> const std::error_category&
{
    class Category : public std::error_category {
    public:
        auto name() const noexcept -> const char* override { return "channel_pipe"; }
        auto message(int ev) const -> std::string override
        {
            if (ev == static_cast<int>(ChannelPipeError::Closed))
                return "channel closed";
            return "unknown channel_pipe error";
        }
    };
    static Category instance;
    return instance;
}

inline auto make_error_code(ChannelPipeError e) -> std::error_code
{
    return { static_cast<int>(e), channel_pipe_category() };
}

// -- Forward declarations ------------------------------------------------------

template<typename T> class ChannelSender;
template<typename T> class ChannelReceiver;
template<typename T> struct PipeCore;
template<typename T> class ChannelSendAwaiter;
template<typename T> class ChannelReceiveAwaiter;

// -- DataOp: pre-allocated reusable carrier ------------------------------------

/**
 * @brief Reusable message carrier and credit token.
 *
 * Pre-allocated once per channel at construction; never heap-allocated on
 * the hot path.  A node is "idle" (in `free_list_`) when `core` is null.
 * When in-flight, `core` keeps `PipeCore` alive during transit.
 *
 * The same node type is used for two roles distinguished by `is_credit_return`:
 *  - `false` (default): data message, delivered to receiver thread.
 *  - `true`:  credit-return batch, delivered back to sender thread.
 */
template<typename T>
struct DataOp final : Operation {
    std::shared_ptr<PipeCore<T>> core;
    std::optional<T> value;
    DataOp<T>* pool_next{ nullptr };
    bool is_credit_return{ false };

    DataOp() = default;

    void complete(int res, std::uint32_t flags) override;
};

// -- One-shot control operations (heap-allocated, not on hot path) -------------

template<typename T>
struct SenderCloseOp final : Operation {
    std::shared_ptr<PipeCore<T>> core;
    explicit SenderCloseOp(std::shared_ptr<PipeCore<T>> c) : core{ std::move(c) } {}
    void complete(int res, std::uint32_t flags) override;
};

template<typename T>
struct ReceiverCloseOp final : Operation {
    std::shared_ptr<PipeCore<T>> core;
    explicit ReceiverCloseOp(std::shared_ptr<PipeCore<T>> c) : core{ std::move(c) } {}
    void complete(int res, std::uint32_t flags) override;
};

// -- PipeCore: shared heap state -----------------------------------------------

template<typename T>
struct PipeCore : std::enable_shared_from_this<PipeCore<T>> {
    // Immutable after construction
    IOContext* const sender_ctx;
    IOContext* const receiver_ctx;
    const std::size_t capacity;
    const std::size_t credit_batch;

    // Node pool (owned by PipeCore)
    std::unique_ptr<DataOp<T>[]> node_storage_;

    // Thread A (sender) only
    DataOp<T>* free_list_{ nullptr };

    std::deque<ChannelSendAwaiter<T>*> pending_sends_;
    bool sender_closed_{ false };

    // Thread B (receiver) only
    //
    // Buffered nodes (with their values) waiting to be consumed.
    // Keeping the node here (not yet recycled) preserves the backpressure invariant:
    //   free_list_.count + data_node_queue_.size() + in-transit nodes == capacity
    std::deque<DataOp<T>*> data_node_queue_;

    // Nodes whose values have been consumed; accumulate until credit_batch,
    // then the whole chain is posted back to the sender as a credit return.
    DataOp<T>* freed_head_{ nullptr };
    std::size_t freed_count_{ 0 };

    std::deque<ChannelReceiveAwaiter<T>*> waiting_receivers_;
    bool sender_done_{ false };
    bool receiver_closed_{ false };

    // Construction

    PipeCore(std::size_t cap, IOContext* sc, IOContext* rc)
        : sender_ctx{ sc }
        , receiver_ctx{ rc }
        , capacity{ cap }
        , credit_batch{ std::max(cap / 4, std::size_t{ 1 }) }
        , node_storage_{ std::make_unique<DataOp<T>[]>(cap) }
    {
        for (std::size_t i = 0; i < cap; ++i)
            node_storage_[i].pool_next = (i + 1 < cap) ? &node_storage_[i + 1] : nullptr;
        free_list_ = cap > 0 ? &node_storage_[0] : nullptr;
    }

    // Thread B: data arrived

    /**
     * @brief Process a received DataOp node on the receiver thread.
     *
     * If a coroutine is already waiting, hands off the value and immediately
     * recycles the node.  Otherwise queues the node in `data_node_queue_` so
     * the value can be pulled later; the node is recycled at that point.
     *
     * Deferred recycling preserves the capacity invariant: a DataOp node
     * is not returned to the sender until its value has been consumed.
     */
    void on_data_arrived(DataOp<T>* op)
    {
        if (receiver_closed_) {
            // Receiver endpoint already closed: drop the payload and do not
            // return credits, so pending/future sends fail via ReceiverCloseOp.
            op->value.reset();
            return;
        }

        if (!waiting_receivers_.empty()) {
            auto* pr = waiting_receivers_.front();
            waiting_receivers_.pop_front();
            pr->value_ = std::move(op->value);
            op->value.reset();
            pr->complete(0, 0);

            // Receiver might have closed synchronously while resuming.
            if (!receiver_closed_)
                recycle_node(op);
        } else {
            data_node_queue_.push_back(op);
        }
    }

    void on_sender_closed()
    {
        sender_done_ = true;
        while (!waiting_receivers_.empty()) {
            auto* pr = waiting_receivers_.front();
            waiting_receivers_.pop_front();
            pr->complete(0, 0);
        }
    }

    /**
     * @brief Recycle a consumed node into the freed chain and trigger a batch
     *        credit return if the threshold is reached.
     *
     * Must run on the receiver thread (Thread B).
     */
    void recycle_node(DataOp<T>* op)
    {
        op->pool_next = freed_head_;
        freed_head_ = op;
        ++freed_count_;
        maybe_return_credits();
    }

    /**
     * @brief Post accumulated freed nodes back to the sender when batch threshold
     *        is reached.
     *
     * Uses the chain head as the Operation posted to sender_ctx.  The head's
     * `core` is set to keep PipeCore alive during transit; other chain nodes
     * piggyback via `pool_next`.
     */
    void maybe_return_credits()
    {
        if (freed_count_ < credit_batch)
            return;

        DataOp<T>* chain = freed_head_;
        freed_head_ = nullptr;
        freed_count_ = 0;

        chain->is_credit_return = true;
        chain->core = this->shared_from_this();

        sender_ctx->add_work();
        sender_ctx->post(chain);
    }

    // Thread A: credit-return chain received

    /**
     * @brief Restore a chain of freed nodes to `free_list_` and wake pending senders.
     *
     * @param head Head of the returned chain (pool_next links the rest).
     */
    void on_credits_returned(DataOp<T>* head)
    {
        while (head) {
            auto* next = head->pool_next;
            head->is_credit_return = false;
            head->pool_next = free_list_;
            free_list_ = head;
            head = next;
        }

        while (free_list_ && !pending_sends_.empty()) {
            auto* ps = pending_sends_.front();
            pending_sends_.pop_front();

            auto* op = free_list_;
            free_list_ = op->pool_next;
            op->pool_next = nullptr;
            op->value.emplace(std::move(ps->value_));
            op->core = this->shared_from_this();

            ps->ok_ = true;
            receiver_ctx->post(op);
            ps->complete(0, 0);
        }
    }

    void on_receiver_closed()
    {
        sender_closed_ = true;
        while (!pending_sends_.empty()) {
            auto* ps = pending_sends_.front();
            pending_sends_.pop_front();
            ps->ok_ = false;
            ps->complete(0, 0);
        }
    }
};

// -- Operation complete() implementations --------------------------------------

template<typename T>
void DataOp<T>::complete(int res, std::uint32_t /*flags*/)
{
    auto local_core = std::move(core);

    if (is_credit_return) {
        local_core->sender_ctx->drop_work();
        if (res != -ECANCELED)
            local_core->on_credits_returned(this);
    } else {
        if (res != -ECANCELED)
            local_core->on_data_arrived(this);
    }
}

template<typename T>
void SenderCloseOp<T>::complete(int res, std::uint32_t /*flags*/)
{
    auto local_core = std::move(core);
    local_core->receiver_ctx->drop_work();
    if (res != -ECANCELED)
        local_core->on_sender_closed();
    delete this;
}

template<typename T>
void ReceiverCloseOp<T>::complete(int res, std::uint32_t /*flags*/)
{
    auto local_core = std::move(core);
    local_core->sender_ctx->drop_work();
    if (res != -ECANCELED)
        local_core->on_receiver_closed();
    delete this;
}

// -- ChannelSendAwaiter<T> / ChannelReceiveAwaiter<T> --------------------------

/**
 * @brief Awaiter returned by `ChannelSender<T>::send()`.
 *
 * Inherits `CancelableOperation` so it satisfies the `cancelable_operation`
 * concept and can be wrapped by `timeout()`, `when_any()`, etc.
 *
 * `cancel()` is safe to call from the sender's IOContext thread because
 * `pending_sends_` lives exclusively on that thread.
 */
template<typename T>
class ChannelSendAwaiter : public CancelableOperation {
    friend class ChannelSender<T>;
    template<typename U> friend struct PipeCore;

public:
    using resume_type = void;

    auto await_ready() noexcept -> bool
    {
        if (core_->sender_closed_) {
            ok_ = false;
            return true;
        }
        if (core_->free_list_ != nullptr) {
            auto* op = core_->free_list_;
            core_->free_list_ = op->pool_next;
            op->pool_next = nullptr;
            op->value.emplace(std::move(value_));
            op->core = core_;
            ok_ = true;
            core_->receiver_ctx->post(op);
            return true;
        }
        return false;
    }

    auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
    {
        handle_ = h;
        core_->pending_sends_.push_back(this);
        return true;
    }

    auto await_resume() -> std::expected<void, std::error_code>
    {
        if (cancelled_)
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        if (!ok_)
            return std::unexpected(make_error_code(ChannelPipeError::Closed));
        return {};
    }

    void complete(int result, std::uint32_t /*flags*/) noexcept override
    {
        if (result == -ECANCELED)
            cancelled_ = true;
        this->resume(handle_, result, 0);
    }

    void cancel() noexcept override
    {
        auto& q = core_->pending_sends_;
        auto it = std::find(q.begin(), q.end(), this);
        if (it != q.end()) {
            q.erase(it);
            complete(-ECANCELED, 0);
        }
    }

    auto context() noexcept -> IOContext& { return *core_->sender_ctx; }

private:
    explicit ChannelSendAwaiter(std::shared_ptr<PipeCore<T>> core, T value)
        : core_{ std::move(core) }, value_{ std::move(value) }
    {}

    std::shared_ptr<PipeCore<T>> core_;
    T value_;
    std::coroutine_handle<> handle_;
    bool ok_{ false };
    bool cancelled_{ false };
};

/**
 * @brief Awaiter returned by `ChannelReceiver<T>::receive()`.
 *
 * Inherits `CancelableOperation` so it satisfies the `cancelable_operation`
 * concept and can be wrapped by `timeout()`, `when_any()`, etc.
 *
 * `cancel()` is safe to call from the receiver's IOContext thread because
 * `waiting_receivers_` lives exclusively on that thread.
 */
template<typename T>
class ChannelReceiveAwaiter : public CancelableOperation {
    friend class ChannelReceiver<T>;
    template<typename U> friend struct PipeCore;

public:
    using resume_type = std::expected<T, std::error_code>;

    auto await_ready() noexcept -> bool { return try_consume(); }

    auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
    {
        handle_ = h;
        core_->waiting_receivers_.push_back(this);
        return true;
    }

    auto await_resume() -> std::expected<T, std::error_code>
    {
        if (cancelled_)
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        if (!value_)
            return std::unexpected(make_error_code(ChannelPipeError::Closed));
        return std::move(*value_);
    }

    void complete(int result, std::uint32_t /*flags*/) noexcept override
    {
        if (result == -ECANCELED)
            cancelled_ = true;
        this->resume(handle_, result, 0);
    }

    void cancel() noexcept override
    {
        auto& q = core_->waiting_receivers_;
        auto it = std::find(q.begin(), q.end(), this);
        if (it != q.end()) {
            q.erase(it);
            complete(-ECANCELED, 0);
        }
    }

    auto context() noexcept -> IOContext& { return *core_->receiver_ctx; }

private:
    explicit ChannelReceiveAwaiter(std::shared_ptr<PipeCore<T>> core)
        : core_{ std::move(core) }
    {}

    /// Try to consume a value without suspending.
    /// Returns true if value_ was filled (or channel is Closed).
    auto try_consume() noexcept -> bool
    {
        if (!core_->data_node_queue_.empty()) {
            auto* node = core_->data_node_queue_.front();
            core_->data_node_queue_.pop_front();
            value_ = std::move(node->value);
            node->value.reset();
            core_->recycle_node(node);
            return true;
        }
        if (core_->sender_done_)
            return true;
        return false;
    }

    std::shared_ptr<PipeCore<T>> core_;
    std::coroutine_handle<> handle_;
    std::optional<T> value_;
    bool cancelled_{ false };
};

// -- ChannelSender<T> ----------------------------------------------------------

/**
 * @brief Producer handle for a pipeline channel.
 *
 * Move-only. All `send()` and `close()` calls must run on the sender's
 * IOContext thread (the one passed to `make_channel`).
 */
template<typename T>
class ChannelSender {
public:
    using SendAwaiter = ChannelSendAwaiter<T>;

    ChannelSender() = default;

    explicit ChannelSender(std::shared_ptr<PipeCore<T>> core)
        : core_{ std::move(core) }
    {}

    ChannelSender(ChannelSender&&) noexcept = default;
    auto operator=(ChannelSender&&) noexcept -> ChannelSender& = default;

    ChannelSender(const ChannelSender&) = delete;
    auto operator=(const ChannelSender&) -> ChannelSender& = delete;

    ~ChannelSender()
    {
        if (core_)
            close();
    }

    /**
     * @brief Asynchronously send a value through the channel.
     *
     * Returns immediately when a credit node is available.
     * Suspends when all nodes are in-flight; resumes when credits return.
     *
     * @return `std::expected<void, std::error_code>`.
     *         Returns `ChannelPipeError::Closed` if channel is closed.
     */
    [[nodiscard]]
    auto send(T value) -> SendAwaiter
    {
        return SendAwaiter{ core_, std::move(value) };
    }

    /**
     * @brief Close the sender side of the channel.  Idempotent.
     *
     * Fails all suspended sends with Closed, then posts `SenderCloseOp`
     * so the receiver can drain its buffer before seeing Closed.
     */
    void close()
    {
        if (!core_ || core_->sender_closed_)
            return;
        core_->sender_closed_ = true;
        while (!core_->pending_sends_.empty()) {
            auto* ps = core_->pending_sends_.front();
            core_->pending_sends_.pop_front();
            ps->ok_ = false;
            ps->complete(0, 0);
        }
        auto* op = new SenderCloseOp<T>{ core_ };
        core_->receiver_ctx->add_work();
        core_->receiver_ctx->post(op);
    }

    [[nodiscard]]
    auto is_closed() const noexcept -> bool { return !core_ || core_->sender_closed_; }

private:
    std::shared_ptr<PipeCore<T>> core_;
};

// -- ChannelReceiver<T> --------------------------------------------------------

/**
 * @brief Consumer handle for a pipeline channel.
 *
 * Move-only. All `receive()` and `close()` calls must run on the receiver's
 * IOContext thread (the one passed to `make_channel`).
 */
template<typename T>
class ChannelReceiver {
public:
    using ReceiveAwaiter = ChannelReceiveAwaiter<T>;

    ChannelReceiver() = default;

    explicit ChannelReceiver(std::shared_ptr<PipeCore<T>> core)
        : core_{ std::move(core) }
    {}

    ChannelReceiver(ChannelReceiver&&) noexcept = default;
    auto operator=(ChannelReceiver&&) noexcept -> ChannelReceiver& = default;

    ChannelReceiver(const ChannelReceiver&) = delete;
    auto operator=(const ChannelReceiver&) -> ChannelReceiver& = delete;

    ~ChannelReceiver()
    {
        if (core_)
            close();
    }

    /**
     * @brief Asynchronously receive a value from the channel.
     *
     * Returns immediately when the internal buffer is non-empty.
     * Suspends when the buffer is empty.
     *
     * @return `std::expected<T, std::error_code>`.
     *         Returns `ChannelPipeError::Closed` when sender closed and buffer is drained.
     */
    [[nodiscard]]
    auto receive() -> ReceiveAwaiter { return ReceiveAwaiter{ core_ }; }

    /**
     * @brief Close the receiver side of the channel.  Idempotent.
     *
     * Wakes all suspended receivers with Closed, then posts `ReceiverCloseOp`
     * so the sender's pending sends are failed.
     */
    void close()
    {
        if (!core_ || core_->receiver_closed_)
            return;
        core_->receiver_closed_ = true;
        while (!core_->waiting_receivers_.empty()) {
            auto* pr = core_->waiting_receivers_.front();
            core_->waiting_receivers_.pop_front();
            pr->complete(0, 0);
        }
        auto* op = new ReceiverCloseOp<T>{ core_ };
        core_->sender_ctx->add_work();
        core_->sender_ctx->post(op);
    }

    [[nodiscard]]
    auto is_closed() const noexcept -> bool
    {
        return !core_ || core_->sender_done_ || core_->receiver_closed_;
    }

private:
    std::shared_ptr<PipeCore<T>> core_;
};

// -- Factory -------------------------------------------------------------------

/**
 * @brief Create a paired (ChannelSender, ChannelReceiver) for cross-thread pipelines.
 *
 * @tparam T           Value type.
 * @param capacity     Buffer capacity and initial credit count.  Must be >= 1.
 * @param sender_ctx   IOContext that will own the sender (Thread A).
 * @param receiver_ctx IOContext that will own the receiver (Thread B).
 * @return A `std::pair` of (ChannelSender<T>, ChannelReceiver<T>).
 *
 * @code
 * auto [tx, rx] = async::make_channel<int>(256, ctx_a, ctx_b);
 * @endcode
 */
template<typename T>
auto make_channel(std::size_t capacity, IOContext& sender_ctx, IOContext& receiver_ctx)
    -> std::pair<ChannelSender<T>, ChannelReceiver<T>>
{
    assert(capacity >= 1 && "channel_pipe: capacity must be >= 1");
    auto core = std::make_shared<PipeCore<T>>(capacity, &sender_ctx, &receiver_ctx);
    return { ChannelSender<T>{ core }, ChannelReceiver<T>{ core } };
}

} // namespace async

#endif // BLOG_ASYNC_CHANNEL_PIPE_H
