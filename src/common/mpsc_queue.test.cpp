#include "mpsc_queue.h"

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

TEST_CASE("MPSCQueue: single producer single consumer", "[mpsc_queue]")
{
    MPSCQueue<Node> q;

    Node n1{}; n1.seq = 1;
    Node n2{}; n2.seq = 2;
    Node n3{}; n3.seq = 3;

    REQUIRE(q.push(&n1));
    REQUIRE(q.push(&n2));
    REQUIRE(q.push(&n3));

    auto* r1 = q.pop();
    auto* r2 = q.pop();
    auto* r3 = q.pop();
    auto* r4 = q.pop();

    REQUIRE(r1 == &n1);
    REQUIRE(r2 == &n2);
    REQUIRE(r3 == &n3);
    REQUIRE(r4 == nullptr);
}

TEST_CASE("MPSCQueue: bounded capacity provides backpressure", "[mpsc_queue]")
{
    MPSCQueue<Node> q{ 2 };

    Node n1{}; n1.seq = 1;
    Node n2{}; n2.seq = 2;
    Node n3{}; n3.seq = 3;

    REQUIRE(q.try_push(&n1));
    REQUIRE(q.try_push(&n2));
    REQUIRE_FALSE(q.try_push(&n3));
    REQUIRE(q.size() == 2);

    REQUIRE(q.pop() == &n1);

    REQUIRE(q.try_push(&n3));

    REQUIRE(q.pop() == &n2);
    REQUIRE(q.pop() == &n3);
    REQUIRE(q.pop() == nullptr);
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
                while (!q.push(&nodes[p][i])) {}
        });
    }

    while (started.load(std::memory_order_acquire) != producer_count) {}
    go.store(true, std::memory_order_release);

    std::array<std::size_t, producer_count> seen{};
    std::size_t popped = 0;

    while (popped < total) {
        if (auto* n = q.pop()) {
            // Per-producer FIFO must hold in MPSC queue.
            REQUIRE(n->seq == seen[n->producer_id]);
            ++seen[n->producer_id];
            ++popped;
        }
    }

    for (auto& t : producers)
        t.join();

    REQUIRE(q.pop() == nullptr);
}
