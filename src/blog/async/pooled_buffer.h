#ifndef BLOG_ASYNC_POOLED_BUFFER_H
#define BLOG_ASYNC_POOLED_BUFFER_H

#include <limits>
#include <span>
#include <utility>

#include "io_context.h"

namespace async {

/**
 * @brief RAII handle for a buffer slice borrowed from a registered io_uring buffer ring.
 *
 * When io_uring delivers a CQE with buffer-select flags, the kernel has chosen
 * a slot from a pre-registered buffer ring. This type wraps that slice and
 * returns it to the ring via `context_->release(bgid, bid)` on destruction,
 * keeping the pool replenished for subsequent reads.
 *
 * Ownership is move-only: copying would cause double-release bugs.
 *
 * @tparam Context Execution context type that owns the buffer rings and
 *                 exposes a `release(bgid, bid)` method.
 */
class PooledBuffer {
public:
    PooledBuffer() = default;

    PooledBuffer(IOContext& context, unsigned bgid, unsigned bid, std::span<std::byte> buffer)
      : context_{ &context }, 
        bgid_{ bgid }, 
        bid_{ bid }, 
        buffer_{ buffer }
    {}

    PooledBuffer(const PooledBuffer&) = delete;
    auto operator=(const PooledBuffer&) -> PooledBuffer& = delete;

    PooledBuffer(PooledBuffer&& other) noexcept
      : context_{ std::exchange(other.context_, nullptr) }, 
        bgid_{ std::exchange(other.bgid_, INVALID_BGID) }, 
        bid_{ std::exchange(other.bid_, INVALID_BID) }, 
        buffer_{ std::exchange(other.buffer_, {}) }
    {}

    auto operator=(PooledBuffer&& other) noexcept -> PooledBuffer&
    {
        if (this == &other) return *this;

        release();

        context_ = std::exchange(other.context_, nullptr);
        bgid_ = std::exchange(other.bgid_, INVALID_BGID);
        bid_ = std::exchange(other.bid_, INVALID_BID);
        buffer_ = std::exchange(other.buffer_, {});

        return *this;
    }

    ~PooledBuffer()
    {
        release();
    }

    /**
     * @brief Return the buffer slice delivered by the kernel.
     *
     * The span is valid until this `PooledBuffer` is destroyed or moved from.
     */
    [[nodiscard]]
    auto data() const noexcept -> std::span<std::byte>
    {
        return buffer_;
    }

    /**
     * @brief Check whether this handle refers to a valid buffer slot.
     *
     * Returns `false` after a move or default construction.
     */
    [[nodiscard]]
    constexpr auto valid() const noexcept -> bool
    {
        return context_ != nullptr && 
               bgid_ != INVALID_BGID && 
               bid_ != INVALID_BID;
    }

private:
    static constexpr unsigned INVALID_BGID = std::numeric_limits<unsigned>::max();
    static constexpr unsigned INVALID_BID = std::numeric_limits<unsigned>::max();

    IOContext* context_{ nullptr };
    unsigned bgid_{ INVALID_BGID };
    unsigned bid_{ INVALID_BID };
    std::span<std::byte> buffer_;

    /**
     * @brief Return the buffer slot to the ring if this handle is valid.
     *
     * Called automatically by the destructor and move-assignment operator.
     * After release, `valid()` returns `false`.
     */
    void release()
    {
        if (valid())
            context_->release_buffer_ring(bgid_, bid_);
    }
};

} // namespace async

#endif // BLOG_ASYNC_POOLED_BUFFER_H