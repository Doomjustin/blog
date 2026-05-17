#ifndef BLOG_ASYNC_CHANNEL_H
#define BLOG_ASYNC_CHANNEL_H

#include <deque>
#include <mutex>

#include "io_context.h"
#include "operation.h"

namespace async {

template<typename T>
class Channel {
public:
    class ReceiveAwaiter;
    class SendAwaiter;

private:
    mutable std::mutex mutex_;
    std::deque<T> buffer_;
    std::deque<ReceiveAwaiter*> receivers_;
    std::deque<SendAwaiter*> senders_;
    std::size_t capacity_{ 0 };
    bool is_closed_{ false };

    auto erase(ReceiveAwaiter* awaiter) noexcept -> bool
    {
        std::scoped_lock locker{ mutex_ };
        auto it = std::find(receivers_.begin(), receivers_.end(), awaiter);
        if (it != receivers_.end()) {
            receivers_.erase(it);
            return true;
        }

        return false;
    }

    auto erase(SendAwaiter* awaiter) noexcept -> bool
    {
        std::scoped_lock locker{ mutex_ };
        auto it = std::find(senders_.begin(), senders_.end(), awaiter);
        if (it != senders_.end()) {
            senders_.erase(it);
            return true;
        }

        return false;
    }

public:
    class SendAwaiter : public Operation {
        friend class ReceiveAwaiter;
        friend class Channel;

    private:
        struct CancelFn {
            SendAwaiter* awaiter;

            void operator()()
            {
                if (awaiter->channel_->erase(awaiter)) {
                    awaiter->result = -ECANCELED;
                    awaiter->context_->post(awaiter);
                }
            }
        };

        Channel* channel_;
        T value_;
        IOContext* context_;
        std::stop_token stop_token_;
        std::optional<std::stop_callback<CancelFn>> stop_callback_;

    public:
        SendAwaiter(Channel* channel, T value)
          : channel_{ channel }
          , value_{ std::move(value) }
        {}

        SendAwaiter(SendAwaiter&&) = default;
        auto operator=(SendAwaiter&&) -> SendAwaiter& = default;

        ~SendAwaiter()
        {
            stop_callback_.reset();
        }

        auto try_send(std::unique_lock<std::mutex>& locker) noexcept -> bool
        {
            if (!channel_->receivers_.empty()) {
                auto* receiver = channel_->receivers_.front();
                channel_->receivers_.pop_front();
                locker.unlock();

                receiver->value_ = std::move(value_);
                receiver->result = 0;
                receiver->context_->post(receiver);
                this->result = 0;
                return true;
            }

            if (channel_->buffer_.size() < channel_->capacity()) {
                channel_->buffer_.push_back(std::move(value_));
                this->result = 0;
                return true;
            }

            return false;
        }

        auto await_ready() noexcept -> bool
        {
            std::unique_lock<std::mutex> locker{ channel_->mutex_ };
            return try_send(locker);
            // return false;
        }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool
        {
            this->handle = handle;

            context_ = handle.promise().context;
            stop_token_ = handle.promise().stop_token;

            if (stop_token_.stop_requested()) {
                this->result = -ECANCELED;
                return false;
            }

            stop_callback_.emplace(stop_token_, CancelFn{ this });

            std::unique_lock<std::mutex> locker{ channel_->mutex_ };

            if (channel_->is_closed_) {
                locker.unlock();

                stop_callback_.reset();
                this->result = -EPIPE;
                return false;
            }

            if (try_send(locker)) {
                stop_callback_.reset();
                return false;
            }

            channel_->senders_.push_back(this);
            return true;
        }

        auto await_resume() noexcept -> std::expected<void, std::error_code>
        {
            if (this->result < 0)
                return unexpected_system_error(-this->result);

            return {};
        }

        void resume(int result, std::uint32_t flags) override
        {
            stop_callback_.reset();
            std::exchange(handle, nullptr).resume();
        }
    };

    class ReceiveAwaiter : public Operation {
        friend class SendAwaiter;
        friend class Channel;

    private:
        struct CancelFn {
            ReceiveAwaiter* awaiter;

            void operator()()
            {
                if (awaiter->channel_->erase(awaiter)) {
                    awaiter->result = -ECANCELED;
                    awaiter->context_->post(awaiter);
                }
            }
        };

        Channel* channel_;

        std::optional<T> value_;
        IOContext* context_;
        std::stop_token stop_token_;
        std::optional<std::stop_callback<CancelFn>> stop_callback_;

        auto try_receive(std::unique_lock<std::mutex>& locker) noexcept -> bool
        {
            if (!channel_->buffer_.empty()) {
                value_ = std::move(channel_->buffer_.front());
                channel_->buffer_.pop_front();

                if (!channel_->senders_.empty()) {
                    auto* sender = channel_->senders_.front();
                    channel_->senders_.pop_front();
                    channel_->buffer_.push_back(std::move(sender->value_));

                    locker.unlock();
                    sender->result = 0;
                    sender->context_->post(sender);
                }

                this->result = 0;
                return true;
            }

            if (!channel_->senders_.empty()) {
                auto* sender = channel_->senders_.front();
                channel_->senders_.pop_front();

                locker.unlock();
                value_ = std::move(sender->value_);
                sender->result = 0;
                sender->context_->post(sender);

                this->result = 0;
                return true;
            }

            return false;
        }

    public:
        ReceiveAwaiter(Channel* channel)
          : channel_{ channel }
        {}

        ReceiveAwaiter(ReceiveAwaiter&&) = default;
        auto operator=(ReceiveAwaiter&&) -> ReceiveAwaiter& = default;

        ~ReceiveAwaiter()
        {
            stop_callback_.reset();
        }

        auto await_ready() noexcept -> bool
        {
            std::unique_lock locker{ channel_->mutex_ };
            return try_receive(locker);
        }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool
        {
            this->handle = handle;

            context_ = handle.promise().context;
            stop_token_ = handle.promise().stop_token;

            if (stop_token_.stop_requested()) {
                this->result = -ECANCELED;
                return false;
            }

            stop_callback_.emplace(stop_token_, CancelFn{ this });

            std::unique_lock locker{ channel_->mutex_ };

            if (channel_->is_closed_ && channel_->buffer_.empty()) {
                locker.unlock();

                stop_callback_.reset();
                this->result = -EPIPE;
                return false;
            }

            if (try_receive(locker)) {
                stop_callback_.reset();
                return false;
            }

            channel_->receivers_.push_back(this);
            return true;
        }

        auto await_resume() noexcept -> std::expected<T, std::error_code>
        {
            if (this->result < 0)
                return unexpected_system_error(-this->result);

            return std::move(*value_);
        }

        void resume(int result, std::uint32_t flags) override
        {
            stop_callback_.reset();
            std::exchange(handle, nullptr).resume();
        }
    };

    explicit Channel(std::size_t capacity = 0)
      : capacity_{ capacity }
    {}

    auto try_receive() noexcept -> std::optional<T>
    {
        std::scoped_lock locker{ mutex_ };
        if (buffer_.empty())
            return {};

        auto value = std::move(buffer_.front());
        buffer_.pop_front();

        if (!senders_.empty()) {
            auto* sender = senders_.front();
            senders_.pop_front();

            buffer_.push_back(std::move(sender->value_));
            sender->result = 0;
            sender->context_->post(sender);
        }

        return value;
    }

    auto try_send(T value) noexcept -> bool
    {
        std::scoped_lock locker{ mutex_ };
        if (buffer_.size() >= capacity_)
            return false;

        buffer_.push_back(std::move(value));
        return true;
    }

    auto async_receive() noexcept -> ReceiveAwaiter
    {
        return ReceiveAwaiter{ this };
    }

    auto async_send(T value) noexcept -> SendAwaiter
    {
        return SendAwaiter{ this, std::move(value) };
    }

    void close() noexcept
    {
        std::unique_lock locker{ mutex_ };
        if (is_closed_)
            return;

        is_closed_ = true;

        while (!senders_.empty()) {
            auto* sender = senders_.front();
            senders_.pop_front();

            sender->result = -EPIPE;
            sender->context_->post(sender);
        }

        while (!receivers_.empty()) {
            auto* receiver = receivers_.front();
            receivers_.pop_front();

            receiver->result = -EPIPE;
            receiver->context_->post(receiver);
        }
    }

    auto is_closed() const noexcept -> bool
    {
        std::scoped_lock locker{ mutex_ };
        return is_closed_;
    }

    auto empty() const noexcept
    {
        std::scoped_lock locker{ mutex_ };
        return buffer_.empty();
    }

    constexpr auto size() const noexcept
    {
        std::scoped_lock locker{ mutex_ };
        return buffer_.size();
    }

    constexpr auto capacity() const noexcept
    {
        return capacity_;
    }
};

} // namespace async

#endif // BLOG_ASYNC_CHANNEL_H