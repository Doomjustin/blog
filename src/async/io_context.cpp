#include "io_context.h"

#include <algorithm>
#include <vector>

#include <liburing.h>

#include <common.h>
#include <operation.h>
#include <this_coroutine.h>

namespace async {

void IOContext::run()
{
    this_coroutine::ContextBinder binder{ *this };

    while (tracking_operations_.load(std::memory_order_relaxed) > 0) {
        // 如果用户调用了stop()，就取消所有未完成的操作
        if (should_stop_.load(std::memory_order_relaxed)) {
            auto* current = head_;
            while (current) {
                cancel(current);
                current = current->next;
            }
        }

        scheduler_.schedule(tracking_operations_);
    }
}

auto IOContext::sqe() noexcept -> ::io_uring_sqe*
{
    return scheduler_.sqe();
}

void IOContext::stop()
{
    should_stop_.store(true, std::memory_order_relaxed);
    scheduler_.wakeup();
}

void IOContext::track(gsl::not_null<Operation*> operation) noexcept
{
    if (!head_) {
        head_ = tail_ = operation;
    }
    else {
        tail_->next = operation;
        operation->prev = tail_;
        tail_ = operation;
    }

    add_work();
}

void IOContext::untrack(gsl::not_null<Operation*> operation) noexcept
{
    if (operation->prev)
        operation->prev->next = operation->next;
    else
        head_ = operation->next;

    if (operation->next)
        operation->next->prev = operation->prev;
    else
        tail_ = operation->prev;

    operation->prev = operation->next = nullptr;
    drop_work();
}

void IOContext::cancel(gsl::not_null<Operation*> operation) noexcept
{
    if (operation->is_canceling_) return;

    if (auto* sqe = scheduler_.sqe()) {
        ::io_uring_prep_cancel(sqe, operation, 0);
        ::io_uring_sqe_set_data(sqe, nullptr);
        operation->is_canceling_ = true;
    }
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
    auto* operation = cross_thread_operations_.pop_all();
    while (operation) {
        auto* next = static_cast<Operation*>(operation->mpsc_next.load(std::memory_order_relaxed));
        operation->complete(-ECANCELED, 0);
        operation = next;
    }

    for (auto* pending : local_operations_)
        pending->complete(-ECANCELED, 0);
    local_operations_.clear();

    ::io_uring_queue_exit(&ring_);
    ::close(wakeup_fd_);
}

auto IOContext::Scheduler::sqe() -> ::io_uring_sqe*
{
    auto* sqe = ::io_uring_get_sqe(&ring_);

    if (!sqe) {
        // 没有可用的SQE了，提交当前的请求以腾出空间
        ::io_uring_submit(&ring_);
        return ::io_uring_get_sqe(&ring_);
    }

    return sqe;
}

void IOContext::Scheduler::schedule(const std::atomic_size_t& tracking)
{
    process_local_operations();

    // If all work finished during local-op processing, don't block in the
    // kernel — the run() loop will see tracking == 0 and exit cleanly.
    if (tracking.load(std::memory_order_relaxed) == 0)
        return;

    unsigned wait_for = local_operations_.empty() ? 1 : 0;
    auto res = ::io_uring_submit_and_wait(&ring_, wait_for);
    if (res < 0) {
        if (res == -EINTR) return;

        throw_system_error("io_uring_submit_and_wait");
    }

    std::vector<PendingEvent> events;
    events.swap(pending_cqe_events_);

    unsigned count{ 0 };
    collect_cqe_events(events, count);

    if (count > 0)
        ::io_uring_cq_advance(&ring_, count);

    dispatch_cqe_events(events);
    events.clear();

    if (pending_cqe_events_.empty())
        pending_cqe_events_.swap(events);
}

void IOContext::Scheduler::collect_cqe_events(std::vector<PendingEvent>& pending_events, unsigned& count) noexcept
{
    unsigned head;
    io_uring_for_each_cqe(&ring_, head, cqe_) {
        ++count;

        if (::io_uring_cqe_get_data64(cqe_) == WAKEUP_MARKER) {
            pending_events.push_back(PendingEvent{ .is_wakeup = true });
            continue;
        }

        if (::io_uring_cqe_get_data64(cqe_) != 0) {
            auto* op = static_cast<Operation*>(::io_uring_cqe_get_data(cqe_));
            pending_events.push_back(PendingEvent{
                .is_wakeup = false,
                .operation = op,
                .result = cqe_->res,
                .flags = cqe_->flags,
            });
        }
    }
}

void IOContext::Scheduler::dispatch_cqe_events(std::vector<PendingEvent>& pending_events) noexcept
{
    for (auto& event : pending_events) {
        if (event.is_wakeup) {
            resume_wakeup();
            process_cross_thread_operations();
            arm_wakeup();
            continue;
        }

        event.operation->complete(event.result, event.flags);
    }
}

void IOContext::Scheduler::wakeup()
{
    std::uint64_t val = 1;
    ::write(wakeup_fd_, &val, sizeof(val));
}

void IOContext::Scheduler::arm_wakeup()
{
    auto* sqe = this->sqe();
    if (!sqe)
        throw_system_error("sqe failed when re-arming wakeup");

    ::io_uring_prep_poll_add(sqe, wakeup_fd_, POLLIN);
    ::io_uring_sqe_set_data64(sqe, WAKEUP_MARKER);
}   

void IOContext::Scheduler::resume_wakeup()
{
    uint64_t val;
    ::read(wakeup_fd_, &val, sizeof(val));
}

void IOContext::Scheduler::process_cross_thread_operations() noexcept
{
    auto* operation = cross_thread_operations_.pop_all();
    while (operation) {
        auto* next = static_cast<Operation*>(operation->mpsc_next.load(std::memory_order_relaxed));
        operation->complete(operation->scheduled_result_, 0);
        operation = next;
    }
}

void IOContext::Scheduler::process_local_operations() noexcept
{
    std::vector<Operation*> pending_operations;
    pending_operations.swap(local_operations_);

    for (auto* operation : pending_operations)
        operation->complete(operation->scheduled_result_, 0);
}

IOContext::BufferRingGroup::~BufferRingGroup()
{
    auto release = [this](BufferRing& buffer) -> void
    {
        if (buffer.base_address) {
            const auto bgid = static_cast<int>(&buffer - group_.data());
            ::io_uring_free_buf_ring(ring_, buffer.buffer, buffer.entries, bgid);
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

    ring_ = ring;
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