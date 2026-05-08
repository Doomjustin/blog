#ifndef BLOG_ASYNC_THREAD_SAFE_CHANNEL_H
#define BLOG_ASYNC_THREAD_SAFE_CHANNEL_H

#include <atomic>
#include <coroutine>
#include <expected>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include <async/io_context.h>
#include <async/operation.h>
#include <common/spinlock.h>
#include <async/this_coroutine.h>

namespace async {

// ── Error category ───────────────────────────────────────────────────────────

enum class ThreadSafeChannelError: std::uint8_t { Closed = 1 };

inline auto thread_safe_channel_category() noexcept -> const std::error_category&
{
    class Category : public std::error_category {
    public:
        auto name() const noexcept -> const char* override { return "thread_safe_channel"; }
        
        auto message(int ev) const -> std::string override
        {
            if (ev == static_cast<int>(ThreadSafeChannelError::Closed))
                return "channel closed";
            return "unknown thread_safe_channel error";
        }
    };
    static Category instance;
    return instance;
}

inline auto make_error_code(ThreadSafeChannelError e) -> std::error_code
{
    return { static_cast<int>(e), thread_safe_channel_category() };
}

// ── ThreadSafeChannel ──────────────────────────────────────────────────────────────────

/**
 * @brief Buffered MPMC async channel.
 *
 * Producers and consumers may run on different `IOContext` threads.
 * Both awaiters derive from `CancelableOperation` so they compose with
 * `when_any` and `timeout`.
 *
 * Use `capacity = 0` for an unbuffered (rendezvous) channel.
 *
 * @tparam T Value type transported through the channel.
 */
template<typename T>
class ThreadSafeChannel {
public:
    class ReceiveAwaiter;
    class SendAwaiter;

    explicit ThreadSafeChannel(std::size_t capacity = 0) : capacity_{ capacity }
    {
        if (capacity_ > 0)
            buffer_.resize(capacity_);
    }

    ThreadSafeChannel(const ThreadSafeChannel&) = delete;
    auto operator=(const ThreadSafeChannel&) -> ThreadSafeChannel& = delete;

    ThreadSafeChannel(ThreadSafeChannel&&) = delete;
    auto operator=(ThreadSafeChannel&&) -> ThreadSafeChannel& = delete;

    ~ThreadSafeChannel() { close(); }

    /**
     * @brief Close the channel.
     *
     * All pending senders and receivers are woken with `ThreadSafeChannelError::Closed`.
     * Future `send` / `receive` calls also fail immediately.
     */
    void close() noexcept
    {
        // Drain both wait queues under the lock, then wake outside.
        IntrusiveList senders;
        IntrusiveList receivers;
        {
            std::scoped_lock lock{ mutex_ };
            if (closed_)
                return;
            closed_ = true;
            senders   = std::exchange(waiting_senders_,   {});
            receivers = std::exchange(waiting_receivers_, {});
        }
        while (!senders.empty()) {
            auto* op = static_cast<SendAwaiter*>(senders.pop_front());
            op->in_queue_ = false;
            wake(op, *op->ctx_, 0);
        }
        while (!receivers.empty()) {
            auto* op = static_cast<ReceiveAwaiter*>(receivers.pop_front());
            op->in_queue_ = false;
            wake(op, *op->ctx_, 0);
        }
    }

    [[nodiscard]]
    auto is_closed() const noexcept -> bool { return closed_.load(std::memory_order_relaxed); }

    // ── SendAwaiter ──────────────────────────────────────────────────────────

    /**
     * @brief Awaiter returned by `send()`.
     *
     * Captures the calling coroutine's `IOContext` so completions are
     * delivered on the correct thread. Integrates with `when_any` / `timeout`.
     */
    class [[nodiscard]] SendAwaiter : public CancelableOperation {
        friend class ThreadSafeChannel;

    public:
        using resume_type = void;

        SendAwaiter(ThreadSafeChannel& ch, IOContext& ctx, T value)
          : ch_{ ch }, 
            ctx_{ &ctx }, 
            value_{ std::move(value) }
        {}

        [[nodiscard]]
        constexpr auto await_ready() const noexcept -> bool { return false; }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
        {
            handle_ = h;
            ctx_->add_work();

            if (!ch_.try_send_or_suspend(this)) {
                ctx_->drop_work();
                return false;
            }
            return true;
        }

        auto await_resume() -> std::expected<void, std::error_code>
        {
            if (cancelled_)
                return std::unexpected(std::make_error_code(std::errc::operation_canceled));

            if (!ok_)
                return std::unexpected(make_error_code(ThreadSafeChannelError::Closed));
            
            return {};
        }

        void complete(int result, std::uint32_t flags) noexcept override
        {
            ctx_->drop_work();
            if (result == -ECANCELED)
                cancelled_ = true;

            this->resume(handle_, result, flags);
        }

        void cancel() noexcept override { ch_.cancel_send(this); }

        auto context() noexcept -> IOContext& { return *ctx_; }

    private:
        ThreadSafeChannel& ch_;
        IOContext* ctx_;
        T value_;
        std::coroutine_handle<> handle_;
        bool ok_ = false;
        bool cancelled_ = false;
        bool in_queue_ = false;
    };

    // ── ReceiveAwaiter ───────────────────────────────────────────────────────

    /**
     * @brief Awaiter returned by `receive()`.
     *
     * Captures the calling coroutine's `IOContext`. Integrates with
     * `when_any` / `timeout`.
     */
    class [[nodiscard]] ReceiveAwaiter : public CancelableOperation {
        friend class ThreadSafeChannel;

    public:
        using resume_type = std::expected<T, std::error_code>;

        ReceiveAwaiter(ThreadSafeChannel& ch, IOContext& ctx) : ch_{ ch }, ctx_{ &ctx } {}

        [[nodiscard]]
        constexpr auto await_ready() const noexcept -> bool { return false; }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
        {
            handle_ = h;
            ctx_->add_work();

            if (!ch_.try_receive_or_suspend(this)) {
                ctx_->drop_work();
                return false;
            }
            return true;
        }

        auto await_resume() -> std::expected<T, std::error_code>
        {
            if (cancelled_)
                return std::unexpected(
                    std::make_error_code(std::errc::operation_canceled));
            if (!value_)
                return std::unexpected(make_error_code(ThreadSafeChannelError::Closed));
            return std::move(*value_);
        }

        void complete(int result, std::uint32_t flags) noexcept override
        {
            ctx_->drop_work();
            if (result == -ECANCELED)
                cancelled_ = true;
            this->resume(handle_, result, flags);
        }

        void cancel() noexcept override { ch_.cancel_receive(this); }

        auto context() noexcept -> IOContext& { return *ctx_; }

    private:
        ThreadSafeChannel& ch_;
        IOContext* ctx_;
        std::coroutine_handle<> handle_;
        std::optional<T> value_;
        bool cancelled_ = false;
        bool in_queue_ = false;
    };

    // ── Public API ───────────────────────────────────────────────────────────

    auto send(T value) -> SendAwaiter
    {
        return SendAwaiter{ *this, this_coroutine::context(), std::move(value) };
    }

    auto receive() -> ReceiveAwaiter
    {
        return ReceiveAwaiter{ *this, this_coroutine::context() };
    }

private:
    // Intrusive linked list reusing Operation::prev / Operation::next.
    // ThreadSafeChannel awaiters are never tracked by IOContext, so these fields
    // are free for use while the awaiter sits in a wait queue.
    struct IntrusiveList {
        Operation* head_{ nullptr };
        Operation* tail_{ nullptr };

        [[nodiscard]] auto empty() const noexcept -> bool { return head_ == nullptr; }

        void push_back(Operation* op) noexcept
        {
            op->next = nullptr;
            op->prev = tail_;
            if (tail_) tail_->next = op;
            else head_ = op;
            tail_ = op;
        }

        auto pop_front() noexcept -> Operation*
        {
            auto* op = head_;
            if (!op) return nullptr;
            head_ = op->next;
            if (head_) head_->prev = nullptr;
            else tail_ = nullptr;
            op->prev = op->next = nullptr;
            return op;
        }

        void erase(Operation* op) noexcept
        {
            if (op->prev) op->prev->next = op->next;
            else head_ = op->next;
            if (op->next) op->next->prev = op->prev;
            else tail_ = op->prev;
            op->prev = op->next = nullptr;
        }
    };

    SpinLock mutex_;
    std::size_t capacity_;
    std::atomic<bool> closed_{ false };

    // Ring buffer for T values. Empty when capacity_ == 0 (rendezvous mode).
    std::vector<T> buffer_;
    std::size_t head_idx_{ 0 };
    std::size_t count_{ 0 };

    IntrusiveList waiting_senders_;
    IntrusiveList waiting_receivers_;

    // A pending wake collected while holding the spinlock, fired after releasing it.
    struct PendingWake {
        CancelableOperation* op{ nullptr };
        IOContext* ctx{ nullptr };
        int result{ 0 };

        explicit operator bool() const noexcept { return op != nullptr; }
        
        void fire() noexcept { wake(op, *ctx, result); }
    };

    // Route op to its owning context: same-thread → submit, cross-thread → post.
    // scheduled_result_ carries the result; no lambda allocation needed.
    static void wake(CancelableOperation* op, IOContext& ctx, int result) noexcept
    {
        op->scheduled_result_ = result;
        if (ctx.is_owner_thread())
            ctx.submit(op);
        else
            ctx.post(op);
    }

    // Returns true if the caller should suspend (op enqueued), false if handled inline.
    auto try_send_or_suspend(SendAwaiter* op) -> bool
    {
        PendingWake w;
        bool suspend = false;
        {
            std::scoped_lock lock{ mutex_ };

            if (closed_)
                return false; // ok_ stays false → Closed error

            // Direct handoff to a waiting receiver.
            if (!waiting_receivers_.empty()) {
                auto* recv = static_cast<ReceiveAwaiter*>(waiting_receivers_.pop_front());
                recv->in_queue_ = false;
                recv->value_.emplace(std::move(op->value_));
                op->ok_ = true;
                w = { recv, recv->ctx_, 0 };
            }
            // Buffer has space.
            else if (count_ < capacity_) {
                buffer_[(head_idx_ + count_) % capacity_] = std::move(op->value_);
                ++count_;
                op->ok_ = true;
            }
            // Buffer full (or rendezvous) — suspend.
            else {
                waiting_senders_.push_back(op);
                op->in_queue_ = true;
                suspend = true;
            }
        }
        if (w) w.fire();
        return suspend;
    }

    auto try_receive_or_suspend(ReceiveAwaiter* op) -> bool
    {
        PendingWake w;
        bool suspend = false;
        {
            std::scoped_lock lock{ mutex_ };

            // Data in buffer — pop and optionally admit a waiting sender.
            if (count_ > 0) {
                op->value_ = std::move(buffer_[head_idx_]);
                head_idx_ = (head_idx_ + 1) % capacity_;
                --count_;

                if (!waiting_senders_.empty()) {
                    auto* snd = static_cast<SendAwaiter*>(waiting_senders_.pop_front());
                    snd->in_queue_ = false;
                    buffer_[(head_idx_ + count_) % capacity_] = std::move(snd->value_);
                    ++count_;
                    snd->ok_ = true;
                    w = { snd, snd->ctx_, 0 };
                }
            }
            // Closed and drained — signal EOF (value_ stays empty).
            else if (closed_) {
                // nothing
            }
            // Waiting sender — direct handoff (unbuffered or race).
            else if (!waiting_senders_.empty()) {
                auto* snd = static_cast<SendAwaiter*>(waiting_senders_.pop_front());
                snd->in_queue_ = false;
                op->value_.emplace(std::move(snd->value_));
                snd->ok_ = true;
                w = { snd, snd->ctx_, 0 };
            }
            // Nothing ready — suspend.
            else {
                waiting_receivers_.push_back(op);
                op->in_queue_ = true;
                suspend = true;
            }
        }
        if (w) w.fire();
        return suspend;
    }

    void cancel_send(SendAwaiter* op) noexcept
    {
        PendingWake w;
        {
            std::scoped_lock lock{ mutex_ };
            if (op->in_queue_) {
                waiting_senders_.erase(op);
                op->in_queue_ = false;
                w = { op, op->ctx_, -ECANCELED };
            }
        }
        if (w) w.fire();
    }

    void cancel_receive(ReceiveAwaiter* op) noexcept
    {
        PendingWake w;
        {
            std::scoped_lock lock{ mutex_ };
            if (op->in_queue_) {
                waiting_receivers_.erase(op);
                op->in_queue_ = false;
                w = { op, op->ctx_, -ECANCELED };
            }
        }
        if (w) w.fire();
    }
};

} // namespace async

#endif // BLOG_ASYNC_THREAD_SAFE_CHANNEL_H
