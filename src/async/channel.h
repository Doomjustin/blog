#ifndef BLOG_ASYNC_CHANNEL_H
#define BLOG_ASYNC_CHANNEL_H

#include <cassert>
#include <coroutine>
#include <expected>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include <io_context.h>
#include <operation.h>
#include <this_coroutine.h>

namespace async {

// ── Error category ───────────────────────────────────────────────────────────

enum class ChannelError : std::uint8_t { Closed = 1 };

inline auto channel_category() noexcept -> const std::error_category&
{
    class Category : public std::error_category {
    public:
        auto name() const noexcept -> const char* override { return "channel"; }

        auto message(int ev) const -> std::string override
        {
            if (ev == static_cast<int>(ChannelError::Closed))
                return "channel closed";
            return "unknown error";
        }
    };
    static Category instance;
    return instance;
}

inline auto make_error_code(ChannelError e) -> std::error_code
{
    return {static_cast<int>(e), channel_category()};
}

// ── Intrusive linked list (O(1) erase) ───────────────────────────────────────

struct IntrusiveOperationList {
    Operation* head_{nullptr};
    Operation* tail_{nullptr};

    [[nodiscard]] 
    auto empty() const noexcept -> bool { return head_ == nullptr; }

    void push_back(Operation* op) noexcept
    {
        op->next = nullptr;
        op->prev = tail_;
        if (tail_)
            tail_->next = op;
        else
            head_ = op;
        tail_ = op;
    }

    auto pop_front() noexcept -> Operation*
    {
        auto* op = head_;
        if (op) {
            head_ = op->next;
            if (head_)
                head_->prev = nullptr;
            else
                tail_ = nullptr;
            op->prev = op->next = nullptr;
        }
        return op;
    }

    void erase(Operation* op) noexcept
    {
        if (op->prev)
            op->prev->next = op->next;
        else
            head_ = op->next;

        if (op->next)
            op->next->prev = op->prev;
        else
            tail_ = op->prev;

        op->prev = op->next = nullptr;
    }
};

/**
 * @brief Lockfree MPMC channel using architectural delegation.
 *
 * Core principle: Channel itself is completely single-threaded (owned by IOContext).
 * Cross-thread sends bypass Channel internals and directly post Operation to target IOContext.
 * No atomic ops on Channel internals — all atomics confined to IOContext's bottom-level MPSC.
 *
 * This achieves "business-layer lockfree" by delegating concurrency control upward.
 *
 * @warning **Owner-thread only.** Every call to `send()`, `receive()`, `close()`, and all
 * internal state mutations must execute on the single `IOContext` thread that owns this
 * channel. Using this channel from a different thread produces a data race and undefined
 * behaviour. For cross-thread pipelines, use `make_channel<T>()` / `ChannelSender` /
 * `ChannelReceiver` instead.
 */
template<typename T>
class Channel {
public:
    class ReceiveAwaiter;
    class SendAwaiter;

    /**
     * @brief Create channel with given buffer capacity.
     * @param capacity 0 = rendezvous, >0 = buffered
     */
    explicit Channel(std::size_t capacity = 0)
        : owner_ctx_{ &this_coroutine::context() }
        , capacity_{ capacity }
    {
        if (capacity_ > 0)
            buffer_.resize(capacity_);
    }

    Channel(const Channel&) = delete;
    Channel(Channel&&) = delete;

    ~Channel() { close(); }

    /**
     * @brief Return the owning IOContext.
     *
     * Can be called safely before any coroutine operation starts.
     */
    [[nodiscard]]
    auto context() const noexcept -> IOContext&
    {
        return *owner_ctx_;
    }

    /**
     * @brief Close the channel.
     *
     * Called only by owner thread. Wakes all suspended senders/receivers.
     * Drain semantics: buffered data consumed before ChannelError::Closed returned.
     */
    void close() noexcept
    {
        assert(owner_ctx_->is_owner_thread() &&
               "Channel::close() must be called from the owner IOContext thread");
        if (closed_) return;

        closed_ = true;

        auto senders = std::exchange(waiting_senders_, {});
        auto receivers = std::exchange(waiting_receivers_, {});

        // Drain and wake all waiters (owner thread context — no atomics needed)
        while (!senders.empty()) {
            auto* op = senders.pop_front();
            auto* sender = static_cast<SendAwaiter*>(op);
            sender->in_queue_ = false;
            wake_awaiter(sender, 0);  // 0 = success, ok_ remains false → Closed error
        }

        while (!receivers.empty()) {
            auto* op = receivers.pop_front();
            auto* receiver = static_cast<ReceiveAwaiter*>(op);
            receiver->in_queue_ = false;
            wake_awaiter(receiver, 0);  // 0 = success, ok_ remains false → Closed error
        }
    }

    [[nodiscard]]
    auto send(T value) -> SendAwaiter 
    {
        return SendAwaiter{*this, std::move(value)}; 
    }

    [[nodiscard]]
    auto receive() -> ReceiveAwaiter 
    { 
        return ReceiveAwaiter{*this}; 
    }

    // ── SendAwaiter ───────────────────────────────────────────────────────

    class [[nodiscard]] SendAwaiter : public CancelableOperation {
        friend class Channel;

    public:
        using resume_type = void;

        SendAwaiter(Channel& ch, T value)
          : ch_{ ch }, ctx_{ &ch_.context() }, value_{ std::move(value) }
        {}

        [[nodiscard]]
        constexpr auto await_ready() const noexcept -> bool { return false; }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
        {
            handle_ = h;
            ctx_ = &this_coroutine::context();
            return ch_.try_send_or_suspend(this);
        }

        auto await_resume() -> std::expected<void, std::error_code>
        {
            if (cancelled_)
                return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            if (!ok_)
                return std::unexpected(make_error_code(ChannelError::Closed));
            return {};
        }

        void complete(int result, std::uint32_t /*flags*/) noexcept override
        {
            if (result == -ECANCELED)
                cancelled_ = true;
            this->resume(handle_, result, 0);
        }

        void cancel() noexcept override { ch_.cancel_send(this); }

        auto context() noexcept -> IOContext& { return *ctx_; }

    private:
        Channel& ch_;
        IOContext* ctx_;
        T value_;
        std::coroutine_handle<> handle_;
        bool ok_{false};
        bool cancelled_{false};
        bool in_queue_{false};
    };

    // ── ReceiveAwaiter ────────────────────────────────────────────────────

    class [[nodiscard]] ReceiveAwaiter : public CancelableOperation {
        friend class Channel;

    public:
        using resume_type = std::expected<T, std::error_code>;

        explicit ReceiveAwaiter(Channel& ch) 
          : ch_{ ch }, ctx_{ &ch_.context() }
        {}

        [[nodiscard]]
        constexpr auto await_ready() const noexcept -> bool { return false; }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
        {
            handle_ = h;
            ctx_ = &this_coroutine::context();
            return ch_.try_receive_or_suspend(this);
        }

        auto await_resume() -> std::expected<T, std::error_code>
        {
            if (cancelled_)
                return std::unexpected(std::make_error_code(std::errc::operation_canceled));

            if (!value_)
                return std::unexpected(make_error_code(ChannelError::Closed));
            
            return std::move(*value_);
        }

        void complete(int result, std::uint32_t /*flags*/) noexcept override
        {
            if (result == -ECANCELED)
                cancelled_ = true;

            this->resume(handle_, result, 0);
        }

        void cancel() noexcept override { ch_.cancel_receive(this); }

        auto context() noexcept -> IOContext& { return *ctx_; }

    private:
        Channel& ch_;
        IOContext* ctx_;
        std::coroutine_handle<> handle_;
        std::optional<T> value_;
        bool cancelled_{false};
        bool in_queue_{false};
    };

private:
    // ── Owner-thread-only state (zero atomics) ─────────────────────────────

    IOContext* const owner_ctx_; ///< IOContext that owns this channel; asserted in hot paths
    std::size_t capacity_;
    bool closed_{false};

    // Ring buffer: pre-allocated at construction, no dynamic allocation per send/receive
    std::vector<T> buffer_;
    std::size_t head_idx_{0};
    std::size_t count_{0};

    // Intrusive lists for O(1) cancel via in_queue_ flag
    IntrusiveOperationList waiting_senders_;
    IntrusiveOperationList waiting_receivers_;

    // ── Helper: Delegate wake to correct thread ────────────────────────────

    /**
     * @brief Wake awaiter in its own IOContext (architectural delegation).
     *
     * Caller thread may differ from awaiter's owner thread.
     * Atomics are hidden in IOContext::post (MPSC queue), not here.
     */
    static void wake_awaiter(SendAwaiter* sender, int result) noexcept
    {
        sender->scheduled_result_ = result;
        sender->ctx_->submit(sender);
    }

    static void wake_awaiter(ReceiveAwaiter* receiver, int result) noexcept
    {
        receiver->scheduled_result_ = result;
        receiver->ctx_->submit(receiver);
    }

    // ── Try operations (called only by owner thread) ────────────────────────

    auto try_send_or_suspend(SendAwaiter* op) -> bool
    {
        assert(owner_ctx_->is_owner_thread() &&
               "Channel: send must be co_await-ed on the owner IOContext thread");

        // Channel closed?
        if (closed_)
            return false; // immediate resume with Closed error

        // Match with waiting receiver?
        if (!waiting_receivers_.empty()) {
            auto* recv = static_cast<ReceiveAwaiter*>(waiting_receivers_.pop_front());
            recv->in_queue_ = false;
            recv->value_.emplace(std::move(op->value_));
            op->ok_ = true;
            wake_awaiter(recv, 0);
            return false; // immediate resume
        }

        // Can buffer?
        if (count_ < capacity_) {
            std::size_t tail_idx = (head_idx_ + count_) % capacity_;
            buffer_[tail_idx] = std::move(op->value_);
            ++count_;
            op->ok_ = true;
            return false; // immediate resume
        }

        // Suspend: add to waiting senders
        waiting_senders_.push_back(op);
        op->in_queue_ = true;
        return true;
    }

    auto try_receive_or_suspend(ReceiveAwaiter* op) -> bool
    {
        assert(owner_ctx_->is_owner_thread() &&
               "Channel: receive must be co_await-ed on the owner IOContext thread");

        // Any buffered data?
        if (count_ > 0) {
            op->value_.emplace(std::move(buffer_[head_idx_]));
            head_idx_ = (head_idx_ + 1) % capacity_;
            --count_;

            // Wake waiting sender?
            if (!waiting_senders_.empty()) {
                auto* snd = static_cast<SendAwaiter*>(waiting_senders_.pop_front());
                snd->in_queue_ = false;
                std::size_t tail_idx = (head_idx_ + count_) % capacity_;
                buffer_[tail_idx] = std::move(snd->value_);
                ++count_;
                snd->ok_ = true;
                wake_awaiter(snd, 0);
            }
            return false; // immediate resume
        }

        // Channel closed?
        if (closed_)
            return false; // immediate resume with Closed error

        // Match with waiting sender (rendezvous)?
        if (!waiting_senders_.empty()) {
            auto* snd = static_cast<SendAwaiter*>(waiting_senders_.pop_front());
            snd->in_queue_ = false;
            op->value_.emplace(std::move(snd->value_));
            snd->ok_ = true;
            wake_awaiter(snd, 0);
            return false; // immediate resume
        }

        // Suspend: add to waiting receivers
        waiting_receivers_.push_back(op);
        op->in_queue_ = true;
        return true;
    }

    void cancel_send(SendAwaiter* op) noexcept
    {
        // Owner thread only
        if (op->in_queue_) {
            waiting_senders_.erase(op); // O(1)
            op->in_queue_ = false;
            wake_awaiter(op, -ECANCELED);
        }
    }

    void cancel_receive(ReceiveAwaiter* op) noexcept
    {
        // Owner thread only
        if (op->in_queue_) {
            waiting_receivers_.erase(op); // O(1)
            op->in_queue_ = false;
            wake_awaiter(op, -ECANCELED);
        }
    }
};

} // namespace async

#endif // BLOG_ASYNC_CHANNEL_H
