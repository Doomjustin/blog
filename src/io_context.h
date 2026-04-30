#ifndef BLOG_IO_CONTEXT_H
#define BLOG_IO_CONTEXT_H

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <optional>

#include <sys/eventfd.h>
#include <sys/poll.h>

#include <liburing.h>
#include <liburing/io_uring.h>

/**
 * @brief Event loop context backed by `io_uring`.
 *
 * Drives completion delivery for coroutine-based async operations. The
 * design targets a core-per-thread model: loop execution and SQE
 * provisioning are expected to happen on one thread, while `stop()` is
 * safe to invoke from other threads.
 *
 * Ring lifecycle and wakeup mechanics are encapsulated in the private
 * `Scheduler` class so that `IOContext` itself only manages the work
 * counter and stop flag. This separation keeps the public API minimal
 * and allows the scheduler to be replaced or extended independently.
 */
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

    /**
     * @brief Initialize `io_uring` and install internal wakeup polling.
     *
     * @param entries SQ/CQ capacity hint passed to `io_uring_queue_init`.
     * @throws std::system_error If ring or wakeup fd initialization fails.
     */
    explicit IOContext(unsigned entries = 1024)
      : scheduler_(entries)
    {}

    IOContext(const IOContext&) = delete;
    auto operator=(const IOContext&) -> IOContext& = delete;

    // Move is intentionally disabled; IOContext owns kernel resources bound to one thread.
    IOContext(IOContext&& other) noexcept = delete;
    auto operator=(IOContext&&) -> IOContext& = delete;

    ~IOContext() = default;

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
        while (!should_stop_.load(std::memory_order_relaxed) && outstanding_works_ > 0)
        {
            auto workdone = scheduler_.schedule();

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
     * @post Outstanding work count is incremented by one when `tracking` is `true`.
     *       Pass `tracking = false` for internal cancel SQEs whose CQE user-data is
     *       `nullptr` and must not be counted as pending work.
     */
    [[nodiscard]]
    auto sqe(bool tracking = true) -> ::io_uring_sqe*
    {
        auto* sqe = scheduler_.sqe();

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
        scheduler_.wakeup();
    }

    /**
     * @brief Access the underlying mutable `io_uring` handle.
     */
    auto ring() noexcept -> ::io_uring*
    {
        return scheduler_.ring();
    }

    /**
     * @brief Access the underlying const `io_uring` handle.
     */
    auto ring() const noexcept -> const ::io_uring*
    {
        return scheduler_.ring();
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
    /**
     * @brief Owns the `io_uring` ring and wakeup fd; drives one completion batch per call.
     *
     * Isolates all kernel-resource lifetime (ring init/exit, eventfd) from
     * the work-tracking and stop-flag logic in `IOContext`. A single
     * `Scheduler` instance is bound to one thread and must not be shared.
     */
    class Scheduler {
    public:
        /**
         * @brief Initialize ring and eventfd; arm the first wakeup poll.
         *
         * @param entries SQ/CQ capacity hint passed to `io_uring_queue_init`.
         * @throws std::system_error If ring or eventfd initialization fails.
         */
        explicit Scheduler(unsigned entries);

        /** @brief Release ring and eventfd kernel resources. */
        ~Scheduler();

        /**
         * @brief Notify the loop via `eventfd` so a blocked `run()` can observe stop state.
         *
         * Thread-safe; may be called from threads other than the loop owner.
         */
        void wakeup();

        auto ring() noexcept -> ::io_uring* { return &ring_; }

        [[nodiscard]]
        auto ring() const noexcept -> const ::io_uring* 
        { 
            return &ring_; 
        }

        /**
         * @brief Obtain a raw SQE from the ring without touching the work counter.
         *
         * The caller is responsible for setting `user_data` and submitting the SQE.
         * Work tracking (if needed) is handled by `IOContext::sqe()`.
         *
         * @throws std::system_error If the submission queue is full.
         */
        auto sqe() -> ::io_uring_sqe*;

        /**
         * @brief Submit pending SQEs, wait for at least one CQE, and dispatch completions.
         *
         * Iterates all available CQEs in one batch: wakeup markers restart the poll
         * SQE; `nullptr` user-data entries (untracked cancel SQEs) are silently
         * skipped; all other entries dispatch `Operation::complete`.
         *
         * @return Number of tracked operations completed in this batch.
         * @throws std::system_error On fatal `io_uring_submit_and_wait` failures
         *         (non-EINTR errors).
         */
        auto schedule() -> unsigned;

    private:
        static constexpr auto WAKEUP_MARKER = std::numeric_limits<std::uintptr_t>::max();

        ::io_uring ring_;
        int wakeup_fd_;
        ::io_uring_cqe* cqe_{ nullptr };

        /**
         * @brief Submit a poll request that listens for wakeup fd readability.
         *
         * The resulting CQE is tagged with `WAKEUP_MARKER` and used only to break
         * blocking wait cycles safely.
         */
        void arm_wakeup();

        /**
         * @brief Drain wakeup fd after receiving a wakeup CQE.
         */
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
    // Tracks background work units that are not represented by immediate CQEs.
    std::size_t outstanding_works_{ 0 };
    // `stop()` can be called cross-thread, so this flag is atomic.
    std::atomic<bool> should_stop_{ false };
};

#endif // BLOG_IO_CONTEXT_H