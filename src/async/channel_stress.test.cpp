#include <atomic>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <all.h>
#include <channel.h>
#include <run.h>
#include <sleep_for.h>
#include <task.h>

using namespace async;
using namespace std::chrono_literals;

/**
 * @brief Stress test: high-frequency producer/consumer to detect data races.
 *
 * This test creates many concurrent producers and consumers exchanging messages
 * at high frequency to detect any data races or deadlocks in the lock-free implementation.
 */

TEST_CASE("channel: concurrent stress test (8 producers, 8 consumers, 10k msgs each)")
{
    constexpr std::size_t producer_count = 8;
    constexpr std::size_t consumer_count = 8;
    constexpr std::size_t messages_per_producer = 10'000;
    constexpr std::size_t buffer_size = 32;

    std::atomic<std::size_t> total_messages_received{0};

    run([=, &total_messages_received]() -> Task<> {
        Channel<int> ch{buffer_size};

        auto producer = [&ch](auto id, auto msg_count) -> Task<> {
            for (std::size_t i = 0; i < msg_count; ++i) {
                auto result = co_await ch.send(static_cast<int>(id * 10000 + i));
                REQUIRE(result);
            }
        };

        auto consumer = [&ch, &total_messages_received]() -> Task<> {
            std::size_t count = 0;
            while (true) {
                auto result = co_await ch.receive();
                if (!result) {
                    REQUIRE(result.error() == make_error_code(ChannelError::Closed));
                    break;
                }
                ++count;
            }
            total_messages_received.fetch_add(count, std::memory_order_acq_rel);
        };

        auto closer = [&ch]() -> Task<> {
            co_await sleep_for(100ms);
            ch.close();
        };

        std::vector<Task<>> tasks;
        for (std::size_t i = 0; i < producer_count; ++i) {
            tasks.push_back(producer(i, messages_per_producer));
        }
        for (std::size_t i = 0; i < consumer_count; ++i) {
            tasks.push_back(consumer());
        }
        tasks.push_back(closer());

        // Execute all tasks concurrently
        // (This would require a version of all() that takes a vector, simplified here)
        co_await all(
            producer(0, messages_per_producer), producer(1, messages_per_producer),
            producer(2, messages_per_producer), producer(3, messages_per_producer),
            producer(4, messages_per_producer), producer(5, messages_per_producer),
            producer(6, messages_per_producer), producer(7, messages_per_producer),
            consumer(), consumer(), consumer(), consumer(),
            consumer(), consumer(), consumer(), consumer(),
            closer()
        );
    });

    // Verify all messages were received
    REQUIRE(total_messages_received == producer_count * messages_per_producer);
}

/**
 * @brief Stress test: rapid channel creation/destruction.
 *
 * Tests that the lock-free channel can be safely created and destroyed
 * many times without leaking resources.
 */

TEST_CASE("channel: rapid channel creation and destruction")
{
    constexpr std::size_t channel_iterations = 1'000;

    run([=]() -> Task<> {
        for (std::size_t i = 0; i < channel_iterations; ++i) {
            Channel<int> ch{16};

            auto producer = [&ch]() -> Task<> {
                for (int j = 0; j < 10; ++j) {
                    auto result = co_await ch.send(j);
                    REQUIRE(result);
                }
            };

            auto consumer = [&ch]() -> Task<> {
                for (int j = 0; j < 10; ++j) {
                    auto result = co_await ch.receive();
                    REQUIRE(result);
                    REQUIRE(result.value() == j);
                }
                ch.close();
            };

            co_await all(producer(), consumer());
        }
    });
}

/**
 * @brief Stress test: mixed send/receive/close operations.
 *
 * Verifies that concurrent close() operations don't cause issues
 * when interleaved with sends and receives.
 */

TEST_CASE("channel: close during active sends/receives")
{
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t messages_per_producer = 1'000;

    run([=]() -> Task<> {
        Channel<int> ch{8};

        auto producer = [&ch](auto id) -> Task<> {
            for (std::size_t i = 0; i < messages_per_producer; ++i) {
                auto result = co_await ch.send(static_cast<int>(id));
                if (!result) {
                    // Channel was closed; this is acceptable
                    break;
                }
            }
        };

        auto consumer = [&ch]() -> Task<> {
            std::size_t count = 0;
            while (count < producer_count * messages_per_producer) {
                auto result = co_await ch.receive();
                if (!result) {
                    break;
                }
                ++count;
            }
        };

        auto delayed_closer = [&ch]() -> Task<> {
            co_await sleep_for(10ms);  // Let some sends happen first
            ch.close();
        };

        co_await all(
            producer(0), producer(1), producer(2), producer(3),
            consumer(), delayed_closer()
        );
    });
}

/**
 * @brief Stress test: large message batches.
 *
 * Tests that Channel handles large buffers and message counts
 * without performance degradation or memory issues.
 */

TEST_CASE("channel: large buffer with many messages")
{
    constexpr std::size_t buffer_size = 1024;
    constexpr std::size_t message_count = 100'000;

    run([=]() -> Task<> {
        Channel<int> ch{buffer_size};

        auto producer = [&ch]() -> Task<> {
            for (std::size_t i = 0; i < message_count; ++i) {
                auto result = co_await ch.send(static_cast<int>(i));
                REQUIRE(result);
            }
        };

        auto consumer = [&ch]() -> Task<> {
            std::size_t count = 0;
            while (true) {
                auto result = co_await ch.receive();
                if (!result) {
                    break;
                }
                ++count;
            }
            REQUIRE(count == message_count);
        };

        auto closer = [&ch]() -> Task<> {
            co_await sleep_for(50ms);
            ch.close();
        };

        co_await all(producer(), consumer(), closer());
    });
}
