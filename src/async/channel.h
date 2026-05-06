#ifndef BLOG_ASYNC_CHANNEL_H
#define BLOG_ASYNC_CHANNEL_H

#include <atomic>
#include <coroutine>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>

#include <io_context.h>
#include <operation.h>
#include <post.h>
#include <this_coroutine.h>

namespace async {

// ── Error category ───────────────────────────────────────────────────────────

enum class ChannelError: std::uint8_t { Closed = 1 };

inline auto channel_category() noexcept -> const std::error_category&
{
    class Category : public std::error_category {
    public:
        auto name() const noexcept -> const char* override { return "channel"; }
        
        auto message(int ev) const -> std::string override
        {
            if (ev == static_cast<int>(ChannelError::Closed))
                return "channel closed";
            return "unknown channel error";
        }
    };
    static Category instance;
    return instance;
}

inline auto make_error_code(ChannelError e) -> std::error_code
{
    return { static_cast<int>(e), channel_category() };
}

// ── Channel ──────────────────────────────────────────────────────────────────

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
class Channel {
public:
    class ReceiveAwaiter;
    class SendAwaiter;

    explicit Channel(std::size_t capacity = 0) : capacity_{ capacity } {}

    Channel(const Channel&) = delete;
    auto operator=(const Channel&) -> Channel& = delete;

    Channel(Channel&&) = delete;
    auto operator=(Channel&&) -> Channel& = delete;

    ~Channel() { close(); }

    /**
     * @brief Close the channel.
     *
     * All pending senders and receivers are woken with `ChannelError::Closed`.
     * Future `send` / `receive` calls also fail immediately.
     */
    void close() noexcept
    {
        std::lock_guard lock{ mutex_ };
        if (closed_)
            return;
        closed_ = true;

        for (auto* op : waiting_senders_)
            wake(op, *op->ctx_, 0);
        waiting_senders_.clear();

        for (auto* op : waiting_receivers_)
            wake(op, *op->ctx_, 0);
        waiting_receivers_.clear();
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
        friend class Channel;

    public:
        SendAwaiter(Channel& ch, IOContext& ctx, T value)
          : ch_{ ch }, 
            ctx_{ &ctx }, 
            value_{ std::move(value) }
        {}

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
                return std::unexpected(make_error_code(ChannelError::Closed));
            
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
        Channel& ch_;
        IOContext* ctx_;
        T value_;
        std::coroutine_handle<> handle_;
        bool ok_ = false;
        bool cancelled_ = false;
    };

    // ── ReceiveAwaiter ───────────────────────────────────────────────────────

    /**
     * @brief Awaiter returned by `receive()`.
     *
     * Captures the calling coroutine's `IOContext`. Integrates with
     * `when_any` / `timeout`.
     */
    class [[nodiscard]] ReceiveAwaiter : public CancelableOperation {
        friend class Channel;

    public:
        ReceiveAwaiter(Channel& ch, IOContext& ctx) : ch_{ ch }, ctx_{ &ctx } {}

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
                return std::unexpected(make_error_code(ChannelError::Closed));
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
        Channel& ch_;
        IOContext* ctx_;
        std::coroutine_handle<> handle_;
        std::optional<T> value_;
        bool cancelled_ = false;
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
    std::mutex mutex_;
    std::size_t capacity_;
    std::atomic<bool> closed_{ false };
    std::deque<T> buffer_;
    std::deque<SendAwaiter*> waiting_senders_;
    std::deque<ReceiveAwaiter*> waiting_receivers_;

    // Wake `op` on `ctx`: same-thread → submit, cross-thread → post.
    static void wake(CancelableOperation* op, IOContext& ctx, int result) noexcept
    {
        dispatch(ctx, [op, result] { op->complete(result, 0); });
    }

    // Returns true if the caller should suspend (op enqueued), false if handled inline.
    auto try_send_or_suspend(SendAwaiter* op) -> bool
    {
        std::scoped_lock lock{ mutex_ };

        if (closed_) {
            // ok_ stays false → Closed error
            return false;
        }

        // Direct handoff to a waiting receiver.
        if (!waiting_receivers_.empty()) {
            auto* recv = waiting_receivers_.front();
            waiting_receivers_.pop_front();
            recv->value_.emplace(std::move(op->value_));
            op->ok_ = true;
            wake(recv, *recv->ctx_, 0);
            return false;
        }

        // Buffer has space.
        if (buffer_.size() < capacity_) {
            buffer_.push_back(std::move(op->value_));
            op->ok_ = true;
            return false;
        }

        // Buffer full — suspend.
        waiting_senders_.push_back(op);
        return true;
    }

    auto try_receive_or_suspend(ReceiveAwaiter* op) -> bool
    {
        std::scoped_lock lock{ mutex_ };

        // Data in buffer — pop and optionally admit a waiting sender.
        if (!buffer_.empty()) {
            op->value_ = std::move(buffer_.front());
            buffer_.pop_front();

            if (!waiting_senders_.empty()) {
                auto* snd = waiting_senders_.front();
                waiting_senders_.pop_front();
                buffer_.push_back(std::move(snd->value_));
                snd->ok_ = true;
                wake(snd, *snd->ctx_, 0);
            }
            return false;
        }

        // Closed and drained — signal EOF (value_ stays empty).
        if (closed_)
            return false;

        // Waiting sender — direct handoff (unbuffered or race).
        if (!waiting_senders_.empty()) {
            auto* snd = waiting_senders_.front();
            waiting_senders_.pop_front();
            op->value_.emplace(std::move(snd->value_));
            snd->ok_ = true;
            wake(snd, *snd->ctx_, 0);
            return false;
        }

        // Nothing ready — suspend.
        waiting_receivers_.push_back(op);
        return true;
    }

    void cancel_send(SendAwaiter* op) noexcept
    {
        std::scoped_lock lock{ mutex_ };
        auto it = std::ranges::find(waiting_senders_, op);
        if (it != waiting_senders_.end()) {
            waiting_senders_.erase(it);
            wake(op, *op->ctx_, -ECANCELED);
        }
    }

    void cancel_receive(ReceiveAwaiter* op) noexcept
    {
        std::scoped_lock lock{ mutex_ };
        auto it = std::ranges::find(waiting_receivers_, op);
        if (it != waiting_receivers_.end()) {
            waiting_receivers_.erase(it);
            wake(op, *op->ctx_, -ECANCELED);
        }
    }
};

} // namespace async

#endif // BLOG_ASYNC_CHANNEL_H
