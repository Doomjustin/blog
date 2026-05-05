#ifndef BLOG_COMMON_MPSC_QUEUE_H
#define BLOG_COMMON_MPSC_QUEUE_H

#include <atomic>
#include <concepts>
#include <cstddef>
#include <limits>

/**
 * @brief Intrusive node base for MPSC queue elements.
 *
 * User node types should derive from this base. Node lifetime is managed by
 * the caller (queue does not allocate/free nodes).
 */
struct MPSCQueueNode {
    std::atomic<MPSCQueueNode*> mpsc_next{ nullptr };
};

template<typename T>
concept mpsc_queue_node = std::derived_from<T, MPSCQueueNode>;

/**
 * @brief Intrusive MPSC (multi-producer, single-consumer) queue.
 *
 * - Producers may call `try_push`/`push` concurrently from multiple threads.
 * - Exactly one consumer thread may call `pop`.
 * - Node lifetime is external to the queue.
 * - Supports optional bounded capacity for backpressure.
 *
 * Implementation follows the classic intrusive linked-list MPSC algorithm
 * with a sentinel stub node. Enqueue uses atomic `exchange` (RMW), not CAS.
 */
template<mpsc_queue_node Node>
class MPSCQueue {
public:
    explicit MPSCQueue(std::size_t capacity = std::numeric_limits<std::size_t>::max())
      : head_{ &stub_ },
        tail_{ &stub_ },
        capacity_{ capacity }
    {}

    MPSCQueue(const MPSCQueue&) = delete;
    auto operator=(const MPSCQueue&) -> MPSCQueue& = delete;

    MPSCQueue(MPSCQueue&&) = delete;
    auto operator=(MPSCQueue&&) -> MPSCQueue& = delete;

    ~MPSCQueue() = default;

    /**
     * @brief Attempt to enqueue one node (thread-safe for producers).
     *
     * Returns `false` when queue has reached configured capacity.
     * This provides non-blocking backpressure.
     */
    auto try_push(Node* node) noexcept -> bool
    {
        auto size_before = size_.fetch_add(1, std::memory_order_acq_rel);
        if (size_before >= capacity_) {
            size_.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }

        push_unchecked(node);
        return true;
    }

    /**
     * @brief Enqueue one node, equivalent to `try_push`.
     *
     * @return `true` on success, `false` when queue is full.
     */
    auto push(Node* node) noexcept -> bool
    {
        return try_push(node);
    }

    /** @brief Approximate number of queued nodes (excluding internal stub). */
    [[nodiscard]]
    auto size() const noexcept -> std::size_t
    {
        return size_.load(std::memory_order_acquire);
    }

    /** @brief Configured capacity (`max` means effectively unbounded). */
    [[nodiscard]]
    constexpr auto capacity() const noexcept -> std::size_t
    {
        return capacity_;
    }

    /**
     * @brief Dequeue one node (single-consumer only).
     *
     * @return Dequeued node pointer, or nullptr when queue is currently empty.
     */
    auto pop() noexcept -> Node*
    {
        auto* head = head_;
        auto* next = static_cast<Node*>(head->mpsc_next.load(std::memory_order_acquire));

        if (head == &stub_) {
            if (!next)
                return nullptr;

            head_ = next;
            head = next;
            next = static_cast<Node*>(head->mpsc_next.load(std::memory_order_acquire));
        }

        if (next) {
            head_ = next;
            size_.fetch_sub(1, std::memory_order_acq_rel);
            return head;
        }

        auto* tail = static_cast<Node*>(tail_.load(std::memory_order_acquire));
        if (head != tail)
            return nullptr;

        // Help progress when consumer catches up to producer publication gap.
        push_unchecked(&stub_);
        next = static_cast<Node*>(head->mpsc_next.load(std::memory_order_acquire));
        if (next) {
            head_ = next;
            size_.fetch_sub(1, std::memory_order_acq_rel);
            return head;
        }

        return nullptr;
    }

private:
    void push_unchecked(Node* node) noexcept
    {
        node->mpsc_next.store(nullptr, std::memory_order_relaxed);

        auto* prev = static_cast<Node*>(tail_.exchange(node, std::memory_order_acq_rel));
        prev->mpsc_next.store(node, std::memory_order_release);
    }

    Node stub_{};
    Node* head_;
    std::atomic<Node*> tail_;
    std::atomic<std::size_t> size_{ 0 };
    std::size_t capacity_;
};

#endif // BLOG_COMMON_MPSC_QUEUE_H
