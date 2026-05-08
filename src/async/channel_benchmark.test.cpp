#include <chrono>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <async/all.h>
#include <async/channel.h>
#include <async/channel_pipe.h>
#include <async/co_spawn.h>
#include <async/run.h>
#include <async/sleep_for.h>
#include <async/task.h>
#include <async/thread_safe_channel.h>

using namespace async;
using namespace std::chrono_literals;

/**
 * @brief Helper to run benchmark scenario with given channel implementation.
 *
 * Template allows benchmarking both ThreadSafeChannel and Channel with identical logic.
 */
template <typename ChannelT>
auto benchmark_producer_consumer(std::size_t message_count, std::size_t buffer_size)
    -> std::chrono::nanoseconds
{
    auto start = std::chrono::high_resolution_clock::now();

    run([=]() -> Task<> {
        ChannelT ch{buffer_size};
        
        auto producer = [&ch](auto msg_count) -> Task<> {
            for (std::size_t i = 0; i < msg_count; ++i) {
                auto result = co_await ch.send(static_cast<int>(i));
                REQUIRE(result);
            }
            ch.close();
        };

        auto consumer = [&ch](auto msg_count) -> Task<> {
            std::size_t count = 0;
            while (true) {
                auto result = co_await ch.receive();
                if (!result) break;  // closed
                ++count;
            }
            REQUIRE(count == msg_count);
        };

        co_await all(producer(message_count), consumer(message_count));
    });

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
}

// ──────────────────────────────────────────────────────────────────────────────
// BENCHMARKS: Single Producer, Single Consumer
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("channel: benchmark spsc (single producer/consumer)", "[!benchmark]")
{
    constexpr std::size_t message_count = 100'000;
    constexpr std::size_t buffer_size = 16;

    BENCHMARK("Channel<int> SPSC 100k messages") {
        return benchmark_producer_consumer<Channel<int>>(message_count, buffer_size);
    };
}

TEST_CASE("thread_safe_channel: benchmark spsc (single producer/consumer)", "[!benchmark]")
{
    constexpr std::size_t message_count = 100'000;
    constexpr std::size_t buffer_size = 16;

    BENCHMARK("ThreadSafeChannel<int> SPSC 100k messages") {
        return benchmark_producer_consumer<ThreadSafeChannel<int>>(message_count, buffer_size);
    };
}

// ──────────────────────────────────────────────────────────────────────────────
// BENCHMARKS: Different buffer sizes
// ──────────────────────────────────────────────────────────────────────────────

TEST_CASE("channel: benchmark buffer size impact", "[!benchmark]")
{
    constexpr std::size_t message_count = 50'000;

    BENCHMARK("Channel<int> buffer_size=1 (unbuffered)") {
        return benchmark_producer_consumer<Channel<int>>(message_count, 1);
    };

    BENCHMARK("Channel<int> buffer_size=8") {
        return benchmark_producer_consumer<Channel<int>>(message_count, 8);
    };

    BENCHMARK("Channel<int> buffer_size=64") {
        return benchmark_producer_consumer<Channel<int>>(message_count, 64);
    };

    BENCHMARK("Channel<int> buffer_size=256") {
        return benchmark_producer_consumer<Channel<int>>(message_count, 256);
    };
}

TEST_CASE("thread_safe_channel: benchmark buffer size impact", "[!benchmark]")
{
    constexpr std::size_t message_count = 50'000;

    BENCHMARK("ThreadSafeChannel<int> buffer_size=1 (unbuffered)") {
        return benchmark_producer_consumer<ThreadSafeChannel<int>>(message_count, 1);
    };

    BENCHMARK("ThreadSafeChannel<int> buffer_size=8") {
        return benchmark_producer_consumer<ThreadSafeChannel<int>>(message_count, 8);
    };

    BENCHMARK("ThreadSafeChannel<int> buffer_size=64") {
        return benchmark_producer_consumer<ThreadSafeChannel<int>>(message_count, 64);
    };

    BENCHMARK("ThreadSafeChannel<int> buffer_size=256") {
        return benchmark_producer_consumer<ThreadSafeChannel<int>>(message_count, 256);
    };
}

// ──────────────────────────────────────────────────────────────────────────────
// BENCHMARKS: Multi-producer scenario
// ──────────────────────────────────────────────────────────────────────────────

template <typename ChannelT>
auto benchmark_multiple_producers_4(std::size_t messages_per_producer, std::size_t buffer_size)
    -> std::chrono::nanoseconds
{
    auto start = std::chrono::high_resolution_clock::now();

    run([=]() -> Task<> {
        ChannelT ch{buffer_size};

        std::size_t remaining_producers = 4;

        auto producer = [&ch, &remaining_producers](auto msg_count) -> Task<> {
            for (std::size_t i = 0; i < msg_count; ++i) {
                auto result = co_await ch.send(static_cast<int>(i));
                REQUIRE(result);
            }
            if (--remaining_producers == 0)
                ch.close();
        };

        auto consumer = [&ch](auto total_msg_count) -> Task<> {
            std::size_t count = 0;
            while (true) {
                auto result = co_await ch.receive();
                if (!result) break;  // closed
                ++count;
            }
            REQUIRE(count == total_msg_count);
        };

        // Run producers_then_close = last producer closes channel
        co_await all(producer(messages_per_producer), producer(messages_per_producer),
                     producer(messages_per_producer), producer(messages_per_producer),
                     consumer(4 * messages_per_producer));
    });

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
}

TEST_CASE("channel: benchmark multiple producers (4 producers)", "[!benchmark]")
{
    constexpr std::size_t messages_per_producer = 25'000;
    constexpr std::size_t buffer_size = 16;

    BENCHMARK("Channel<int> MPSC 4 producers x 25k msgs") {
        return benchmark_multiple_producers_4<Channel<int>>(messages_per_producer,
                                                                    buffer_size);
    };
}

TEST_CASE("thread_safe_channel: benchmark multiple producers (4 producers)", "[!benchmark]")
{
    constexpr std::size_t messages_per_producer = 25'000;
    constexpr std::size_t buffer_size = 16;

    BENCHMARK("ThreadSafeChannel<int> MPSC 4 producers x 25k msgs") {
        return benchmark_multiple_producers_4<ThreadSafeChannel<int>>(messages_per_producer, buffer_size);
    };
}

// ──────────────────────────────────────────────────────────────────────────────
// BENCHMARKS: ChannelPipe (cross-thread SPSC, two IOContexts)
//
// ChannelPipe is fundamentally SPSC and requires two separate IOContexts
// (sender thread A + receiver thread B). This benchmark runs ctx_b in a
// background std::jthread so the two event loops execute in parallel,
// matching the intended production topology.
// ──────────────────────────────────────────────────────────────────────────────

static auto benchmark_pipe(std::size_t message_count, std::size_t capacity)
    -> std::chrono::nanoseconds
{
    auto start = std::chrono::high_resolution_clock::now();

    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(capacity, ctx_a, ctx_b);

    auto sender = [tx = std::move(tx), message_count]() mutable -> Task<> {
        for (std::size_t i = 0; i < message_count; ++i) {
            auto r = co_await tx.send(static_cast<int>(i));
            REQUIRE(r);
        }
        tx.close();
    };

    auto receiver = [rx = std::move(rx), message_count]() mutable -> Task<> {
        std::size_t count = 0;
        while (auto v = co_await rx.receive())
            ++count;
        REQUIRE(count == message_count);
    };

    co_spawn(receiver(), ctx_b);
    std::jthread thread_b([&ctx_b] { ctx_b.run(); });
    co_spawn(sender(), ctx_a);
    ctx_a.run();
    // thread_b joins here

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
}

static auto benchmark_thread_safe_channel_cross_thread(std::size_t message_count,
                                                       std::size_t capacity)
    -> std::chrono::nanoseconds
{
    auto start = std::chrono::high_resolution_clock::now();

    IOContext ctx_a;
    IOContext ctx_b;
    ThreadSafeChannel<int> ch{ capacity };

    auto sender = [&ch, message_count]() mutable -> Task<> {
        for (std::size_t i = 0; i < message_count; ++i) {
            auto r = co_await ch.send(static_cast<int>(i));
            REQUIRE(r);
        }
    };

    auto receiver = [&ch, message_count]() mutable -> Task<> {
        for (std::size_t i = 0; i < message_count; ++i) {
            auto v = co_await ch.receive();
            REQUIRE(v);
        }
    };

    co_spawn(receiver(), ctx_b);
    std::jthread thread_b([&ctx_b] { ctx_b.run(); });
    co_spawn(sender(), ctx_a);
    ctx_a.run();
    // thread_b joins here

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
}

TEST_CASE("channel_pipe: benchmark spsc (single producer/consumer)", "[!benchmark]")
{
    constexpr std::size_t message_count = 100'000;
    constexpr std::size_t capacity = 16;

    BENCHMARK("ChannelPipe<int> SPSC 100k messages") {
        return benchmark_pipe(message_count, capacity);
    };
}

TEST_CASE("cross_thread: benchmark spsc compare channel_pipe vs thread_safe_channel",
          "[!benchmark]")
{
    constexpr std::size_t message_count = 20'000;
    constexpr std::size_t capacity = 16;

    BENCHMARK("ChannelPipe<int> cross-thread SPSC 20k messages") {
        return benchmark_pipe(message_count, capacity);
    };

    BENCHMARK("ThreadSafeChannel<int> cross-thread SPSC 20k messages") {
        return benchmark_thread_safe_channel_cross_thread(message_count, capacity);
    };
}

TEST_CASE("channel_pipe: benchmark capacity impact", "[!benchmark]")
{
    constexpr std::size_t message_count = 50'000;

    BENCHMARK("ChannelPipe<int> capacity=1") {
        return benchmark_pipe(message_count, 1);
    };

    BENCHMARK("ChannelPipe<int> capacity=8") {
        return benchmark_pipe(message_count, 8);
    };

    BENCHMARK("ChannelPipe<int> capacity=64") {
        return benchmark_pipe(message_count, 64);
    };

    BENCHMARK("ChannelPipe<int> capacity=256") {
        return benchmark_pipe(message_count, 256);
    };
}
