#ifndef BLOG_ASYNC_IO_CONTEXT_H
#define BLOG_ASYNC_IO_CONTEXT_H

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <optional>
#include <thread>

#include <sys/eventfd.h>
#include <sys/poll.h>

#include <gsl/gsl>
#include <gsl/pointers>
#include <liburing.h>

#include <common.h>
#include <operation.h>

namespace async {

/**
 * @brief Per-thread event loop wrapping a single io_uring instance.
 *
 * Each worker thread created by `async::run()` owns one `IOContext`.  The loop
 * drives coroutine scheduling by submitting SQEs for pending operations and
 * dispatching CQEs to their `Operation::complete()` handlers.  An internal work
 * counter (`add_work` / `drop_work`) keeps the loop alive until all outstanding
 * work units have been accounted for.
 *
 * `IOContext` is non-copyable and non-movable; instances are always accessed
 * through `this_coroutine::context()` which returns the thread-local reference.
 */
class IOContext {
public:
    /** @brief Metadata for one registered io_uring buffer ring group. */
    struct BufferRing {
        void* base_address{ nullptr };
        unsigned size{ 0 };
        unsigned mask{ 0 };
        unsigned entries{ 0 };
        std::uint16_t tail{ 0 };
        ::io_uring_buf_ring* buffer{ nullptr };
    };

    /**
     * @brief Initialize the io_uring instance with the given submission-queue depth.
     *
     * @param entries Submission-queue size passed to `io_uring_queue_init`.
     */
    explicit IOContext(unsigned entries = 1024)
      : scheduler_{ entries }
    {}

    IOContext(const IOContext&) = delete;
    auto operator=(const IOContext&) -> IOContext& = delete;

    IOContext(IOContext&& other) noexcept = delete;
    auto operator=(IOContext&&) -> IOContext& = delete;

    ~IOContext() = default;

    /**
     * @brief Block the calling thread until all work is completed.
     *
     * Processes SQEs and CQEs in a loop, dispatching each completion to the
     * matching `Operation::complete()` handler. Returns when the work counter
     * reaches zero (or `stop()` is called).
     */
    void run();

    /**
     * @brief Obtain the next available submission-queue entry.
     *
     * The caller must prepare the SQE and set its user-data before the ring is
     * flushed by `run()`.
     *
     * @return Pointer to a fresh, zeroed SQE ready for preparation.
     */
    [[nodiscard]]
    auto sqe() noexcept -> ::io_uring_sqe*;

    /**
     * @brief Signal the event loop to exit.
     *
     * Posts a wakeup event so `run()` unblocks. The loop drains remaining CQEs
     * before returning.  Idiomatic shutdown: call `stop()` then join the thread
     * that owns this context.
     */
    void stop();

    /** @brief Expose the raw `io_uring` handle for low-level interop. */
    auto ring() noexcept -> ::io_uring*
    {
        return scheduler_.ring();
    }

    /** @brief Expose the raw `io_uring` handle (const overload). */
    [[nodiscard]]
    auto ring() const noexcept -> const ::io_uring*
    {
        return scheduler_.ring();
    }

    /**
     * @brief Register an in-flight operation and increment the work counter.
     *
     * Must be paired with a matching `untrack()` call once all CQEs for the
     * operation have been received.
     *
     * @param operation Non-null pointer to the operation being tracked.
     */
    void track(gsl::not_null<Operation*> operation) noexcept;

    /**
     * @brief Deregister a completed operation and decrement the work counter.
     *
     * @param operation Non-null pointer to the operation being untracked.
     */
    void untrack(gsl::not_null<Operation*> operation) noexcept;

    /**
     * @brief Submit an `IORING_OP_ASYNC_CANCEL` SQE targeting `operation`.
     *
     * Does not increment the work counter. The target operation will still
     * deliver a CQE (typically with `-ECANCELED`) that must be handled normally.
     *
     * @param operation In-flight operation to cancel.
     */
    void cancel(gsl::not_null<Operation*> operation) noexcept;

    /**
     * @brief Increment the outstanding-work counter.
     *
     * Called by `DetachedTask::promise_type` on construction. Keeps the event
     * loop alive while detached coroutines are running.
     */
    void add_work() noexcept
    {
        ++tracking_operations_;
    }

    /**
     * @brief Decrement the outstanding-work counter.
     *
     * Called by `DetachedTask::promise_type` on destruction. When the counter
     * reaches zero, `run()` returns.
     */
    void drop_work() noexcept
    {
        assert(tracking_operations_ > 0);
        --tracking_operations_;
    }

    /**
     * @brief Register a new buffer ring with io_uring.
     *
     * @param entries Number of buffers (must be a power of two).
     * @param size    Byte size of each buffer slot.
     * @return Buffer group ID (`bgid`) for use with `recv_multishot` and `PooledBuffer`.
     */
    [[nodiscard]]
    auto setup_buffer_ring(unsigned entries, unsigned size) -> unsigned
    {
        return buffers_.setup(scheduler_.ring(), entries, size);
    }

    /**
     * @brief Return a consumed buffer slot back to the ring.
     *
     * Called automatically by `PooledBuffer`'s destructor.
     *
     * @param bgid Buffer group ID.
     * @param bid  Buffer index within the group.
     */
    void release_buffer_ring(unsigned bgid, unsigned bid)
    {
        buffers_.release(bgid, bid);
    }

    /**
     * @brief Set the default buffer group used when no explicit group is given.
     *
     * @param bgid Buffer group ID to designate as the default.
     */
    void default_buffer(unsigned bgid)
    {
        buffers_.default_buffer(bgid);
    }

    /**
     * @brief Return the default buffer group ID, if one has been set.
     *
     * @return The default `bgid`, or `std::nullopt` if none is configured.
     */
    auto default_buffer() -> std::optional<unsigned>
    {
        return buffers_.default_buffer();
    }

    /**
     * @brief Access the `BufferRing` metadata for the given group.
     *
     * @param bgid Buffer group ID returned by `setup_buffer_ring()`.
     * @return Reference to the corresponding `BufferRing` descriptor.
     */
    auto buffer_ring(unsigned bgid) -> BufferRing&
    {
        return buffers_.buffer_ring(bgid);
    }

    void post(gsl::not_null<Operation*> operation) noexcept
    {
        scheduler_.post(operation);
    }

    [[nodiscard]]
    auto is_owner_thread() const noexcept -> bool
    {
        return std::this_thread::get_id() == thread_id_;
    }

    void submit(gsl::not_null<Operation*> operation) noexcept
    {
        scheduler_.submit(operation);
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

        void post(gsl::not_null<Operation*> operation) noexcept
        {
            cross_thread_operations_.push(operation);
            wakeup();
        }

        void submit(gsl::not_null<Operation*> operation) noexcept
        {
            local_operations_.push_back(operation);
        }

    private:
        static constexpr auto WAKEUP_MARKER = std::numeric_limits<std::uintptr_t>::max();

        ::io_uring ring_;
        int wakeup_fd_;
        ::io_uring_cqe* cqe_{ nullptr };

        MPSCQueue<Operation> cross_thread_operations_;
        std::vector<Operation*> local_operations_;

        void arm_wakeup();

        void resume_wakeup();
        
        void process_cross_thread_operations() noexcept;

        void process_local_operations() noexcept;
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
        ::io_uring* ring_{ nullptr };
        std::array<BufferRing, GROUP_SIZE> group_;
        unsigned next_bgid_{ INIT_BGID };
        std::optional<unsigned> default_buffer_bgid_;
    };

    Scheduler scheduler_;
    BufferRingGroup buffers_;

    Operation* head_{ nullptr };
    Operation* tail_{ nullptr };
    std::thread::id thread_id_{ std::this_thread::get_id() };
    std::size_t tracking_operations_{ 0 };
    std::atomic<bool> should_stop_{ false };
};

} // namespace async

#endif // BLOG_ASYNC_IO_CONTEXT_H
