#include <common/mpsc_queue.h>

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

struct Node : MPSCQueueNode {
    std::size_t producer_id{ 0 };
    std::size_t seq{ 0 };
};

} // namespace

TEST_CASE("MPSCQueue: single producer single consumer via pop_all (FIFO)", "[mpsc_queue]")
{
    MPSCQueue<Node> q;

    Node n1{}; n1.seq = 1;
    Node n2{}; n2.seq = 2;
    Node n3{}; n3.seq = 3;

    q.push(&n1);
    q.push(&n2);
    q.push(&n3);

    // pop_all() reverses to restore FIFO order
    auto* list = q.pop_all();
    REQUIRE(list == &n1);

    auto* l2 = static_cast<Node*>(list->mpsc_next.load(std::memory_order_relaxed));
    REQUIRE(l2 == &n2);

    auto* l3 = static_cast<Node*>(l2->mpsc_next.load(std::memory_order_relaxed));
    REQUIRE(l3 == &n3);
    REQUIRE(l3->mpsc_next.load(std::memory_order_relaxed) == nullptr);

    REQUIRE(q.pop_all() == nullptr);
}

TEST_CASE("MPSCQueue: multi producer single consumer", "[mpsc_queue]")
{
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t per_producer = 5000;
    constexpr std::size_t total = producer_count * per_producer;

    MPSCQueue<Node> q;

    std::vector<std::unique_ptr<Node[]>> nodes;
    nodes.resize(producer_count);
    for (std::size_t p = 0; p < producer_count; ++p) {
        nodes[p] = std::make_unique<Node[]>(per_producer);
        for (std::size_t i = 0; i < per_producer; ++i) {
            nodes[p][i].producer_id = p;
            nodes[p][i].seq = i;
        }
    }

    std::atomic<std::size_t> started{ 0 };
    std::atomic<bool> go{ false };

    std::vector<std::thread> producers;
    producers.reserve(producer_count);

    for (std::size_t p = 0; p < producer_count; ++p) {
        producers.emplace_back([&, p] {
            started.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {}
            for (std::size_t i = 0; i < per_producer; ++i)
                q.push(&nodes[p][i]);
        });
    }

    while (started.load(std::memory_order_acquire) != producer_count) {}
    go.store(true, std::memory_order_release);

    std::array<std::size_t, producer_count> seen{};
    std::size_t popped = 0;

    while (popped < total) {
        auto* list = q.pop_all();
        while (list) {
            auto* next = static_cast<Node*>(list->mpsc_next.load(std::memory_order_relaxed));
            ++seen[list->producer_id];
            ++popped;
            list = next;
        }
    }

    for (auto& t : producers)
        t.join();

    for (std::size_t p = 0; p < producer_count; ++p)
        REQUIRE(seen[p] == per_producer);

    REQUIRE(q.pop_all() == nullptr);
}
