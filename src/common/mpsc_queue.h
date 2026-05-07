#ifndef BLOG_COMMON_MPSC_QUEUE_H
#define BLOG_COMMON_MPSC_QUEUE_H

#include <atomic>
#include <concepts>

struct MPSCQueueNode {
    std::atomic<MPSCQueueNode*> mpsc_next{ nullptr };

    MPSCQueueNode() = default;
    
    MPSCQueueNode(MPSCQueueNode&& other) noexcept 
      : mpsc_next{ nullptr } 
    {}
    
    auto operator=(MPSCQueueNode&& other) noexcept -> MPSCQueueNode& 
    { 
        mpsc_next = nullptr; 
        return *this; 
    }
};

template<typename T>
concept mpsc_queue_node = std::derived_from<T, MPSCQueueNode>;

template<mpsc_queue_node Node>
class MPSCQueue {
public:
    MPSCQueue() = default;

    MPSCQueue(const MPSCQueue&) = delete;
    auto operator=(const MPSCQueue&) -> MPSCQueue& = delete;
    MPSCQueue(MPSCQueue&&) = delete;
    auto operator=(MPSCQueue&&) -> MPSCQueue& = delete;
    ~MPSCQueue() = default;

    auto push(Node* node) noexcept -> bool
    {
        auto* expected = head_.load(std::memory_order_relaxed);
        do {
            node->mpsc_next.store(expected, std::memory_order_relaxed);
        } while (!head_.compare_exchange_weak(
                     expected, node,
                     std::memory_order_release,
                     std::memory_order_relaxed));

        return expected == nullptr;
    }

    [[nodiscard]]
    auto empty() const noexcept -> bool
    {
        return head_.load(std::memory_order_acquire) == nullptr;
    }

    auto pop_all() noexcept -> Node*
    {
        auto* list = static_cast<Node*>(head_.exchange(nullptr, std::memory_order_acquire));
        if (!list)
            return nullptr;

        Node* prev = nullptr;
        while (list) {
            auto* next = static_cast<Node*>(list->mpsc_next.load(std::memory_order_relaxed));
            list->mpsc_next.store(prev, std::memory_order_relaxed);
            prev = list;
            list = next;
        }
        return prev;
    }

private:
    std::atomic<MPSCQueueNode*> head_{ nullptr };
};

#endif // BLOG_COMMON_MPSC_QUEUE_H
