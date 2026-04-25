#ifndef BLOG_IO_CONTEXT_H
#define BLOG_IO_CONTEXT_H

#include <cassert>
#include <cstdlib>

#include <sys/eventfd.h>
#include <sys/poll.h>

#include <liburing.h>
#include <spdlog/spdlog.h>

#include "exceptions.h"
#include "operation.h"

/**
 * @brief Event loop context backed by `io_uring`.
 *
 * This type owns one ring instance and drives completion delivery for
 * coroutine operations. The design targets a core-per-thread model: loop
 * execution and SQE provisioning are expected to happen on one thread,
 * while `stop()` is safe to invoke from other threads.
 */
class IOContext {
public:
    /**
     * @brief Initialize `io_uring` and install internal wakeup polling.
     *
     * @param entries SQ/CQ capacity hint passed to `io_uring_queue_init`.
     * @throws std::system_error If ring or wakeup fd initialization fails.
     */
    explicit IOContext(unsigned entries = 1024)
    {
        if (auto res = ::io_uring_queue_init(entries, &ring_, 0); res < 0)
            throw_system_error(-res, "io_uring_queue_init");            

        wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeup_fd_ == -1)
            throw_system_error("Failed to create eventfd for stopping IOContext");

        arm_wakeup();
    }

    IOContext(const IOContext&) = delete;
    auto operator=(const IOContext&) -> IOContext& = delete;

    // Move is intentionally disabled; IOContext owns kernel resources bound to one thread.
    IOContext(IOContext&& other) noexcept = delete;
    auto operator=(IOContext&&) -> IOContext& = delete;

    /**
     * @brief Release owned kernel resources.
     *
     * The destructor closes wakeup fd and exits the ring. It does not throw.
     */
    ~IOContext()
    {
        ::io_uring_queue_exit(&ring_);
        ::close(wakeup_fd_);
    }

    /**
     * @brief Run the completion loop until stopped or no outstanding work.
     *
     * The loop submits pending SQEs, waits for at least one CQE, dispatches
     * `Operation::complete`, and updates tracked work counters.
     *
     * @throws std::system_error On fatal `io_uring_submit_and_wait` failures.
     */
    void run()
    {
        ::io_uring_cqe* cqe{ nullptr };

        while (!should_stop_.load(std::memory_order_relaxed) && outstanding_works_ > 0)
        {
            auto res = ::io_uring_submit_and_wait(&ring_, 1);
            if (res < 0) {
                if (res == -EINTR)
                    continue;

                throw_system_error("io_uring_submit_and_wait");
            }
                
            unsigned head;
            unsigned count{ 0 };
            unsigned workdone{ 0 };

            io_uring_for_each_cqe(&ring_, head, cqe) {
                ++count;

                if (io_uring_cqe_get_data64(cqe) == WAKEUP_MARKER) {
                    resume_wakeup();
                    arm_wakeup();
                    continue;
                }

                if (io_uring_cqe_get_data64(cqe) != 0) {
                    auto* op = static_cast<Operation*>(io_uring_cqe_get_data(cqe));
                    op->complete(cqe->res, cqe->flags);

                    ++workdone;
                }
            }

            if (count > 0)
                ::io_uring_cq_advance(&ring_, count);

            if (workdone > 0)
                outstanding_works_ -= workdone;
        }
    }

    /**
     * @brief Acquire an SQE and mark one tracked work item.
     *
     * Use this when preparing an operation that will eventually produce a CQE
     * and call `Operation::complete`.
     *
     * @return Available SQE from the owned ring.
     * @throws std::system_error If no SQE can be obtained.
     * @post Outstanding work count is incremented by one.
     */
    [[nodiscard]]
    auto sqe(bool tracking = true) -> ::io_uring_sqe*
    {
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe)
            throw_system_error("io_uring_get_sqe");

        if (tracking)
            add_work();
        
        return sqe;
    }

    /**
     * @brief Request loop termination and wake a blocked `run()`.
     *
     * This method is thread-safe and may be called from threads other than
     * the loop owner.
     */
    void stop()
    {
        should_stop_.store(true, std::memory_order_relaxed);
        wakeup();
    }

    /**
     * @brief Access the underlying mutable `io_uring` handle.
     */
    auto ring() noexcept -> ::io_uring*
    {
        return &ring_;
    }

    /**
     * @brief Access the underlying const `io_uring` handle.
     */
    auto ring() const noexcept -> const ::io_uring*
    {
        return &ring_;
    }

    /**
     * @brief Increment outstanding work counter.
     *
     * Used by spawned/background paths that keep the loop alive independently
     * of immediate SQE submission.
     */
    void add_work() noexcept
    {
        ++outstanding_works_;
    }

    /**
     * @brief Decrement outstanding work counter.
     *
     * @pre Outstanding work count must be greater than zero.
     */
    void drop_work() noexcept
    {
        assert(outstanding_works_ > 0);
        --outstanding_works_;
    }
    
private:
    static constexpr auto WAKEUP_MARKER = std::numeric_limits<std::uintptr_t>::max();

    ::io_uring ring_{};
    int wakeup_fd_{ -1 };

    // Tracks background work units that are not represented by immediate CQEs.
    std::size_t outstanding_works_{ 0 };
    // `stop()` can be called cross-thread, so this flag is atomic.
    std::atomic<bool> should_stop_{ false };

    /**
     * @brief Submit a poll request that listens for wakeup fd readability.
     *
     * The resulting CQE is tagged with `WAKEUP_MARKER` and used only to break
     * blocking wait cycles safely.
     */
    void arm_wakeup()
    {
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe)
            throw_system_error("io_uring_get_sqe failed when re-arming wakeup");

        ::io_uring_prep_poll_add(sqe, wakeup_fd_, POLLIN);
        ::io_uring_sqe_set_data64(sqe, WAKEUP_MARKER);
    }   
    
    /**
     * @brief Notify the loop via `eventfd` so `run()` can observe stop state.
     */
    void wakeup()
    {
        std::uint64_t val = 1;
        ::write(wakeup_fd_, &val, sizeof(val));
    }

    /**
     * @brief Drain wakeup fd after receiving a wakeup CQE.
     */
    void resume_wakeup()
    {
        uint64_t val;
        ::read(wakeup_fd_, &val, sizeof(val));
    }
};

#endif // BLOG_IO_CONTEXT_H