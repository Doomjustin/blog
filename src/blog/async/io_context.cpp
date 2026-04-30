#include "io_context.h"

#include <algorithm>

#include <liburing.h>

#include "common/exceptions.h"
#include "operation.h"

namespace async {

void IOContext::run()
{
    while (!should_stop_.load(std::memory_order_relaxed) && outstanding_works_ > 0)
    {
        auto workdone = scheduler_.schedule();

        if (workdone > 0)
            outstanding_works_ -= workdone;
    }
}

auto IOContext::sqe(bool tracking) -> ::io_uring_sqe*
{
    auto* sqe = scheduler_.sqe();

    if (tracking)
        add_work();
    
    return sqe;
}

void IOContext::stop()
{
    should_stop_.store(true, std::memory_order_relaxed);
    scheduler_.wakeup();
}


IOContext::Scheduler::Scheduler(unsigned entries)
{
    if (auto res = ::io_uring_queue_init(entries, &ring_, 0); res < 0)
        throw_system_error(-res, "io_uring_queue_init");            

    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ == -1)
        throw_system_error("Failed to create eventfd for stopping IOContext");

    arm_wakeup();
}

IOContext::Scheduler::~Scheduler()
{
    ::io_uring_queue_exit(&ring_);
    ::close(wakeup_fd_);
}

auto IOContext::Scheduler::sqe() -> ::io_uring_sqe*
{
    auto* sqe = ::io_uring_get_sqe(&ring_);
    if (!sqe)
        throw_system_error("io_uring_get_sqe");

    return sqe;
}

auto IOContext::Scheduler::schedule() -> unsigned
{
    auto res = ::io_uring_submit_and_wait(&ring_, 1);
    if (res < 0) {
        if (res == -EINTR)
            return 0;

        throw_system_error("io_uring_submit_and_wait");
    }
        
    unsigned head;
    unsigned count{ 0 };
    unsigned workdone{ 0 };

    io_uring_for_each_cqe(&ring_, head, cqe_) {
        ++count;

        if (::io_uring_cqe_get_data64(cqe_) == WAKEUP_MARKER) {
            resume_wakeup();
            arm_wakeup();
            continue;
        }

        if (::io_uring_cqe_get_data64(cqe_) != 0) {
            auto* op = static_cast<Operation*>(::io_uring_cqe_get_data(cqe_));
            op->complete(cqe_->res, cqe_->flags);

            // 如果这个CQE的flags里有IORING_CQE_F_MORE，
            // 说明这个操作后续还有CQE要处理，那就不应该减少outstanding_works_，因为它还没有完成；
            if (cqe_->flags & IORING_CQE_F_MORE)
                continue;

            ++workdone;
        }
    }

    if (count > 0)
        ::io_uring_cq_advance(&ring_, count);

    return workdone;
}

void IOContext::Scheduler::wakeup()
{
    std::uint64_t val = 1;
    ::write(wakeup_fd_, &val, sizeof(val));
}

void IOContext::Scheduler::arm_wakeup()
{
    auto* sqe = ::io_uring_get_sqe(&ring_);
    if (!sqe)
        throw_system_error("io_uring_get_sqe failed when re-arming wakeup");

    ::io_uring_prep_poll_add(sqe, wakeup_fd_, POLLIN);
    ::io_uring_sqe_set_data64(sqe, WAKEUP_MARKER);
}   

void IOContext::Scheduler::resume_wakeup()
{
    uint64_t val;
    ::read(wakeup_fd_, &val, sizeof(val));
}


IOContext::BufferRingGroup::~BufferRingGroup()
{
    auto release = [this](BufferRing& buffer) -> void
    {
        if (buffer.base_address) {
            const auto dealloc_size = static_cast<std::size_t>(buffer.entries * buffer.size);
            memory_resource_->deallocate(buffer.base_address, dealloc_size, ALIGNMENT);
            buffer.base_address = nullptr;
        }
    };

    std::ranges::for_each(group_, release);
}

auto IOContext::BufferRingGroup::setup(::io_uring* ring, unsigned entries, unsigned size) -> unsigned
{
    if (next_bgid_ > MAX_BGID)
        throw std::runtime_error("Exceeded maximum number of ring buffers");

    auto bgid = next_bgid_++;
    auto& buffer_ring = group_[bgid];

    buffer_ring.size = size;
    buffer_ring.entries = entries;
    buffer_ring.mask = ::io_uring_buf_ring_mask(entries);

    const auto alloc_size = static_cast<std::size_t>(entries * size);
    buffer_ring.base_address = memory_resource_->allocate(alloc_size, ALIGNMENT);

    int res = 0;
    buffer_ring.buffer = ::io_uring_setup_buf_ring(ring, entries, bgid, 0, &res);
    if (!buffer_ring.buffer) {
        memory_resource_->deallocate(buffer_ring.base_address, alloc_size, ALIGNMENT);
        buffer_ring.base_address = nullptr;
        throw_system_error(-res, "Failed to setup buffer ring");
    }

    auto* base = static_cast<std::byte*>(buffer_ring.base_address);
    for (unsigned i = 0; i < entries; ++i)
        ::io_uring_buf_ring_add(buffer_ring.buffer, base + i * size, size, i, buffer_ring.mask, i);

    ::io_uring_buf_ring_advance(buffer_ring.buffer, entries);

    buffer_ring.tail = entries;

    if (!default_buffer_bgid_)
        default_buffer_bgid_ = bgid;

    return bgid;
}

void IOContext::BufferRingGroup::release(unsigned bgid, unsigned bid)
{
    if (bgid > MAX_BGID)
        throw std::out_of_range("Buffer group ID exceeds maximum");

    auto& buffer_ring = group_[bgid];
    auto* base = static_cast<std::byte*>(buffer_ring.base_address);
    const int offset = buffer_ring.tail & buffer_ring.mask;
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

void IOContext::BufferRingGroup::default_buffer(unsigned bgid)
{
    if (bgid > MAX_BGID)
        throw std::out_of_range("Buffer group ID exceeds maximum");

    default_buffer_bgid_ = bgid;
}

} // namespace async