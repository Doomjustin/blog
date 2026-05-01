#ifndef BLOG_ASYNC_IO_CONTEXT_H
#define BLOG_ASYNC_IO_CONTEXT_H

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <optional>

#include <sys/eventfd.h>
#include <sys/poll.h>

#include <gsl/gsl>
#include <liburing.h>

#include "async/operation.h"

namespace async {

class IOContext {
public:
    struct BufferRing {
        void* base_address{ nullptr };
        unsigned size{ 0 };
        unsigned mask{ 0 };
        unsigned entries{ 0 };
        std::uint16_t tail{ 0 };
        ::io_uring_buf_ring* buffer{ nullptr };
    };

    explicit IOContext(unsigned entries = 1024)
      : scheduler_{ entries }
    {}

    IOContext(const IOContext&) = delete;
    auto operator=(const IOContext&) -> IOContext& = delete;

    IOContext(IOContext&& other) noexcept = delete;
    auto operator=(IOContext&&) -> IOContext& = delete;

    ~IOContext() = default;

    void run();

    [[nodiscard]]
    auto sqe() -> ::io_uring_sqe*;

    void stop();

    auto ring() noexcept -> ::io_uring*
    {
        return scheduler_.ring();
    }

    [[nodiscard]]
    auto ring() const noexcept -> const ::io_uring*
    {
        return scheduler_.ring();
    }

    void track(gsl::not_null<Operation*> operation) noexcept;

    void untrack(gsl::not_null<Operation*> operation) noexcept;

    void cancel(gsl::not_null<Operation*> operation) noexcept;

    void add_work() noexcept
    {
        ++tracking_operations_;
    }

    void drop_work() noexcept
    {
        assert(tracking_operations_ > 0);
        --tracking_operations_;
    }

    auto setup_buffer_ring(unsigned entries, unsigned size) -> unsigned
    {
        return buffers_.setup(scheduler_.ring(), entries, size);
    }
    
    void release_buffer_ring(unsigned bgid, unsigned bid)
    {
        buffers_.release(bgid, bid);
    }
 
    void default_buffer(unsigned bgid)
    {
        buffers_.default_buffer(bgid);
    }

    auto default_buffer() -> std::optional<unsigned>
    {
        return buffers_.default_buffer();
    }

    auto buffer_ring(unsigned bgid) -> BufferRing&
    {
        return buffers_.buffer_ring(bgid);
    }

private:
    class Scheduler {
    public:
        explicit Scheduler(unsigned entries);

        /** @brief Release ring and eventfd kernel resources. */
        ~Scheduler();

        void wakeup();

        auto ring() noexcept -> ::io_uring* { return &ring_; }

        [[nodiscard]]
        auto ring() const noexcept -> const ::io_uring* 
        { 
            return &ring_; 
        }

        auto sqe() -> ::io_uring_sqe*;

        void schedule();

    private:
        static constexpr auto WAKEUP_MARKER = std::numeric_limits<std::uintptr_t>::max();

        ::io_uring ring_;
        int wakeup_fd_;
        ::io_uring_cqe* cqe_{ nullptr };

        void arm_wakeup();

        void resume_wakeup();
    };

    class BufferRingGroup {
    public:
        explicit BufferRingGroup(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
          : memory_resource_{ resource }
        {}

        ~BufferRingGroup();

        auto setup(::io_uring* ring, unsigned entries, unsigned size) -> unsigned;

        void release(unsigned bgid, unsigned bid);

        void default_buffer(unsigned bgid);

        auto default_buffer() -> std::optional<unsigned>
        { 
            return default_buffer_bgid_; 
        }

        [[nodiscard]]
        constexpr auto empty() const noexcept -> bool
        {
            return next_bgid_ == INIT_BGID;
        }

        [[nodiscard]]
        constexpr auto size() const noexcept -> std::size_t
        {
            return static_cast<std::size_t>(next_bgid_);
        }

        auto buffer_ring(unsigned bgid) noexcept -> BufferRing&
        {
            assert(bgid < GROUP_SIZE);
            return group_[bgid];
        }

    private:
        static constexpr unsigned GROUP_SIZE = 16;
        static constexpr unsigned MAX_BGID{ GROUP_SIZE - 1 };
        static constexpr std::size_t ALIGNMENT = 4096;
        static constexpr unsigned INIT_BGID{ 0 };

        std::pmr::memory_resource* memory_resource_;
        std::array<BufferRing, GROUP_SIZE> group_;
        unsigned next_bgid_{ INIT_BGID };
        std::optional<unsigned> default_buffer_bgid_;
    };

    Scheduler scheduler_;
    BufferRingGroup buffers_;

    Operation* head_{ nullptr };
    Operation* tail_{ nullptr };
    std::size_t tracking_operations_{ 0 };
    std::atomic<bool> should_stop_{ false };
};

} // namespace async

#endif // BLOG_ASYNC_IO_CONTEXT_H