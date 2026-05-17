#include "io_context.h"

#include <utility>

namespace async {

void Executor::process_cross_thread_awaiters() noexcept
{
    auto* awaiter = cross_thread_awaiters_.pop_all();
    while (awaiter) {
        auto* next = static_cast<Operation*>(awaiter->mpsc_next.load(std::memory_order_relaxed));

        XIN_TSAN_ACQUIRE(awaiter);
        awaiter->resume(awaiter->result, awaiter->flags);
        awaiter = next;
    }
}

void Executor::process_local_awaiters() noexcept
{
    std::vector<Operation*> awaiters;
    std::swap(awaiters, local_awaiters_);

    for (auto* awaiter : awaiters)
        awaiter->resume(awaiter->result, awaiter->flags);
}

void IOContext::enqueue_cancel(Operation* awaiter) noexcept
{
    auto* node = new CancelNode{ awaiter->id };
    cancel_queue_.push(node);
    wakeup();
}

auto IOContext::prepare_cancel_sqe(std::uint64_t id) noexcept -> bool
{
    if (auto* cancel_sqe = sqe()) {
        ::io_uring_prep_cancel64(cancel_sqe, id, 0);
        ::io_uring_sqe_set_data64(cancel_sqe, CANCEL_MARKER);
        return true;
    }

    return false;
}

void IOContext::cancel(std::uint64_t id) noexcept
{
    if (pending_tasks_.contains(id) && prepare_cancel_sqe(id))
        ::io_uring_submit(&ring_);
}

void IOContext::process_cancel() noexcept
{
    auto* node = cancel_queue_.pop_all();
    while (node) {
        prepare_cancel_sqe(node->id);
        ::io_uring_submit(&ring_);
        auto* next = static_cast<CancelNode*>(node->mpsc_next.load(std::memory_order_relaxed));
        delete node;
        node = next;
    }
}

void IOContext::arm_wakeup() noexcept
{
    auto* wakeup_sqe = sqe();
    ::io_uring_prep_poll_multishot(wakeup_sqe, wakeup_fd_, POLLIN);
    ::io_uring_sqe_set_data64(wakeup_sqe, WAKEUP_MARKER);
}

void IOContext::wakeup() const noexcept
{
    uint64_t one = 1;
    ::write(wakeup_fd_, &one, sizeof(one));
}

void IOContext::resume_wakeup() const noexcept
{
    uint64_t buffer;
    ::read(wakeup_fd_, &buffer, sizeof(buffer));
}

void IOContext::schedule()
{
    executor_.execute();
    process_cancel();

    if (tracking_works_.load(std::memory_order_relaxed) == 0)
        return;

    auto res = ::io_uring_submit_and_wait(&ring_, 1);
    if (res < 0) {
        if (res == -EINTR)
            return;

        throw_system_error(-res, "io_uring_submit_and_wait failed");
    }

    unsigned count = 0;
    unsigned head;
    ::io_uring_cqe* cqe{ nullptr };

    io_uring_for_each_cqe(&ring_, head, cqe)
    {
        ++count;

        auto user_data = ::io_uring_cqe_get_data64(cqe);

        if (user_data == WAKEUP_MARKER) {
            resume_wakeup();
            if (!(cqe->flags & IORING_CQE_F_MORE))
                arm_wakeup();

            continue;
        }

        if (user_data == CANCEL_MARKER)
            continue;

        if (user_data != 0) {
            if (auto it = pending_tasks_.find(user_data); it != pending_tasks_.end()) {
                auto* awaiter = it->second;
                untrack(it->second);
                XIN_TSAN_ACQUIRE(awaiter);
                awaiter->resume(cqe->res, cqe->flags);
            }
        }
    }

    if (count > 0)
        ::io_uring_cq_advance(&ring_, count);
}

IOContext::IOContext(unsigned entries)
{
    if (auto res = ::io_uring_queue_init(entries, &ring_, 0); res < 0)
        throw_system_error(-res, "io_uring_queue_init failed");

    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ < 0)
        throw_system_error("created eventfd failed");

    arm_wakeup();
    ::io_uring_submit(&ring_);
}

IOContext::~IOContext()
{
    ::io_uring_queue_exit(&ring_);

    if (wakeup_fd_ != -1)
        ::close(wakeup_fd_);
}

void IOContext::run()
{
    thread_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);

    while (tracking_works_.load(std::memory_order_relaxed) > 0) {
        if (should_stop_.load(std::memory_order_relaxed)) {
            // 取消所有未完成的 awaiter
            for (auto& [id, _] : pending_tasks_)
                prepare_cancel_sqe(id);

            ::io_uring_submit(&ring_);
            should_stop_.store(false, std::memory_order_relaxed);
        }

        schedule();
    }
}

void IOContext::stop() noexcept
{
    should_stop_.store(true, std::memory_order_relaxed);
    wakeup();
}

void IOContext::post(Operation* awaiter) noexcept
{
    if (executor_.post(awaiter))
        wakeup();
}

void IOContext::track(::io_uring_sqe* sqe, Operation* awaiter) noexcept
{
    if (awaiter && !pending_tasks_.contains(awaiter->id)) {
        awaiter->id = generate_id();
        pending_tasks_.emplace(awaiter->id, awaiter);
    }

    if (sqe)
        ::io_uring_sqe_set_data64(sqe, awaiter->id);

    tracking_works_.fetch_add(1, std::memory_order_relaxed);
}

void IOContext::untrack(Operation* awaiter) noexcept
{
    if (awaiter)
        pending_tasks_.erase(awaiter->id);

    tracking_works_.fetch_sub(1, std::memory_order_relaxed);
}

void IOContext::cancel(Operation& awaiter) noexcept
{
    if (is_owner_thread())
        cancel(awaiter.id);
    else
        enqueue_cancel(&awaiter);
}

auto IOContext::sqe() noexcept -> ::io_uring_sqe*
{
    auto* sqe = ::io_uring_get_sqe(&ring_);
    if (!sqe) {
        ::io_uring_submit(&ring_);
        return ::io_uring_get_sqe(&ring_);
    }

    return sqe;
}

} // namespace async