#include "receive_stream.h"

#include <utility>

#include "async/operation.h"
#include "common/exceptions.h"

namespace net {

class ReceiveStream::MutishotReceiveOperation: public async::Operation {
    friend class ReceiveStream;

public:
    MutishotReceiveOperation(ReceiveStream* stream, ReceiveStream::context_type* context, unsigned bgid)
      : stream_{ stream }, 
        context_{ context }, 
        bgid_{ bgid } 
    {}

    void detach() noexcept
    {
        stream_ = nullptr;
    }

    void complete(int res, std::uint32_t flags) override
    {
        const bool has_more = (flags & IORING_CQE_F_MORE) != 0;

        if (!has_more)
            context_->untrack(this);

        if (stream_) {
            if (!has_more) {
                stream_->operation_armed_ = false;
                stream_->operation_ = nullptr;
            }

            stream_->handle_cqe(res, flags);
        }
        else if (!stream_ && (flags & IORING_CQE_F_BUFFER)) {
            const auto bid = static_cast<std::uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
            auto& buffer_ring = context_->buffer_ring(bgid_);
            assert(bid < buffer_ring.entries);

            auto* base = static_cast<std::byte*>(buffer_ring.base_address);

            const auto offset = buffer_ring.tail & buffer_ring.mask;

            ::io_uring_buf_ring_add(
                buffer_ring.buffer,
                base + bid * buffer_ring.size,
                buffer_ring.size,
                bid,
                buffer_ring.mask,
                offset
            );

            ::io_uring_buf_ring_advance(buffer_ring.buffer, 1);
            ++buffer_ring.tail;
        }

        if (!has_more)
            delete this;
    }

private:
    ReceiveStream* stream_;
    context_type* context_;
    unsigned bgid_;
};

auto ReceiveStream::NextAwaiter::await_ready() const noexcept -> bool
{
    return !stream_.ready_results_.empty();
}

void ReceiveStream::NextAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    stream_.handle_ = handle;

    // 如果当前没有正在进行的操作，就立刻提交一个新的recv_multishot；
    // 如果有了，说明它完成后会resume这个协程，我们就不需要再提交了
    if (!stream_.operation_armed_)
        stream_.arm_operation();
}

auto ReceiveStream::NextAwaiter::await_resume() -> std::expected<resume_type, std::error_code>
{
    if (stream_.ready_results_.empty())
        return unexpected_system_error(std::errc::operation_canceled);

    // 从对列里取出一个结果返回；
    // 如果是取消操作导致的CQE，那么这个结果就是一个unexpected error，
    // 调用者会得到一个std::error_code为operation_canceled的错误
    auto result = std::move(stream_.ready_results_.front());
    stream_.ready_results_.pop_front();
    return result;
}


ReceiveStream::ReceiveStream(context_type& context, int fd, unsigned bgid)
  : context_{ &context }, 
    fd_{ fd }, 
    bgid_{ bgid }
{}

ReceiveStream::ReceiveStream(ReceiveStream&& other) noexcept
    : context_{ std::exchange(other.context_, nullptr) },
    fd_{ std::exchange(other.fd_, -1) },
    bgid_{ std::exchange(other.bgid_, 0) },
    operation_{ std::exchange(other.operation_, nullptr) },
    operation_armed_{ std::exchange(other.operation_armed_, false) },
    handle_{ std::exchange(other.handle_, nullptr) },
    ready_results_{ std::move(other.ready_results_) }
{
    if (operation_)
        operation_->stream_ = this;
}

auto ReceiveStream::operator=(ReceiveStream&& other) noexcept -> ReceiveStream&
{
    if (this == &other) return *this;

    destroy();

    context_ = std::exchange(other.context_, nullptr);
    fd_ = std::exchange(other.fd_, -1);
    bgid_ = std::exchange(other.bgid_, 0);
    operation_ = std::exchange(other.operation_, nullptr);
    operation_armed_ = std::exchange(other.operation_armed_, false);
    handle_ = std::exchange(other.handle_, nullptr);
    ready_results_ = std::move(other.ready_results_);

    if (operation_)
        operation_->stream_ = this;

    return *this;
}

ReceiveStream::~ReceiveStream()
{
    destroy();
}

void ReceiveStream::arm_operation()
{
    if (!operation_)
        operation_ = new MutishotReceiveOperation{ this, context_, bgid_ };

    auto* sqe = context_->sqe();
    ::io_uring_prep_recv_multishot(sqe, fd_, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = bgid_;
    ::io_uring_sqe_set_data(sqe, static_cast<async::Operation*>(operation_));
    operation_armed_ = true;

    context_->track(operation_);
}

void ReceiveStream::destroy() noexcept
{
    if (operation_) {
        operation_->detach();

        context_->cancel(operation_);
        operation_ = nullptr;
    }

    if (context_)
        context_ = nullptr;
}

void ReceiveStream::handle_cqe(int result, std::uint32_t flags) noexcept
{
    std::optional<PooledBuffer> pooled_buffer;

    if (flags & IORING_CQE_F_BUFFER) {
        const auto bid = static_cast<std::uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
        auto& buffer_ring = context_->buffer_ring(bgid_);
        assert(bid < buffer_ring.entries);

        auto* base = static_cast<std::byte*>(buffer_ring.base_address);
        std::span<std::byte> data{ base + bid * buffer_ring.size, static_cast<std::size_t>(result) };
        
        pooled_buffer.emplace(*context_, bgid_, bid, data);
    }

    if (result == -ECANCELED) {
        ready_results_.emplace_back(unexpected_system_error(std::errc::operation_canceled));
    }
    else if (result >= 0) {
        if (pooled_buffer)
            ready_results_.emplace_back(std::move(*pooled_buffer));
        else
            ready_results_.emplace_back(PooledBuffer{});
    }
    else {
        ready_results_.emplace_back(unexpected_system_error(-result));
    }

    if (handle_) {
        auto handle = std::exchange(handle_, nullptr);
        handle.resume();
    }
}

} // namespace net