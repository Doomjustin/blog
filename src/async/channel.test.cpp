#include <async/channel.h>

#include <atomic>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <async/all.h>
#include <async/run.h>
#include <async/sleep_for.h>
#include <async/task.h>

using namespace std::chrono_literals;
using namespace async;

// ── Test helpers ──────────────────────────────────────────────────────────

static auto send_value(Channel<int>& ch, int value) -> Task<>
{
    auto result = co_await ch.send(value);
    REQUIRE(result);
}

static auto receive_value(Channel<int>& ch, int expected) -> Task<>
{
    auto result = co_await ch.receive();
    REQUIRE(result);
    REQUIRE(*result == expected);
}

static auto drain_channel(Channel<int>& ch, std::vector<int>& out) -> Task<>
{
    while (auto v = co_await ch.receive()) {
        out.push_back(*v);
    }
}

TEST_CASE("channel: basic compilation and types")
{
    auto ch = Channel<int>{1};

    // Verify SendAwaiter and ReceiveAwaiter are defined
    static_assert(std::is_same_v<decltype(ch.send(42))::resume_type, void>);
    static_assert(std::is_same_v<decltype(ch.receive())::resume_type,
                                 std::expected<int, std::error_code>>);

    // Verify error type
    auto err = make_error_code(ChannelError::Closed);
    REQUIRE(err.category().name() == std::string("channel"));
}

TEST_CASE("channel: unbuffered rendezvous between two tasks")
{
    auto entry = []() -> Task<> {
        Channel<int> ch{0};

        auto producer = [&]() -> Task<> {
            co_await send_value(ch, 42);
        };

        auto consumer = [&]() -> Task<> {
            co_await receive_value(ch, 42);
            ch.close();
        };

        co_await all(producer(), consumer());
    };

    run(entry);
}

TEST_CASE("channel: producer and consumer tasks communicate via buffered channel")
{
    std::vector<int> out;

    auto entry = [&]() -> Task<> {
        Channel<int> ch{3};

        auto producer = [&]() -> Task<> {
            for (int i = 1; i <= 5; ++i) {
                co_await send_value(ch, i);
            }
        };

        auto consumer = [&]() -> Task<> {
            co_await drain_channel(ch, out);
        };

        auto closer = [&]() -> Task<> {
            co_await sleep_for(50ms);
            ch.close();
        };

        co_await all(producer(), consumer(), closer());
    };

    run(entry);
    REQUIRE(out == std::vector<int>{1, 2, 3, 4, 5});
}

TEST_CASE("channel: sender suspends when buffer is full, resumes as consumer drains")
{
    std::vector<int> out;

    auto entry = [&]() -> Task<> {
        Channel<int> ch{2};

        auto producer = [&]() -> Task<> {
            for (int i = 0; i < 5; ++i) {
                auto result = co_await ch.send(i * 10);
                REQUIRE(result);
            }
        };

        auto consumer = [&]() -> Task<> {
            co_await drain_channel(ch, out);
        };

        auto closer = [&]() -> Task<> {
            co_await sleep_for(50ms);
            ch.close();
        };

        co_await all(producer(), consumer(), closer());
    };

    run(entry);
    REQUIRE(out == std::vector<int>{0, 10, 20, 30, 40});
}

TEST_CASE("channel: close wakes suspended receiver with Closed error")
{
    bool receiver_woken = false;

    auto entry = [&]() -> Task<> {
        Channel<int> ch{0};

        auto receiver = [&]() -> Task<> {
            auto result = co_await ch.receive();
            REQUIRE(!result);
            REQUIRE(result.error() == make_error_code(ChannelError::Closed));
            receiver_woken = true;
        };

        auto closer = [&]() -> Task<> {
            co_await sleep_for(10ms);
            ch.close();
        };

        co_await all(receiver(), closer());
    };

    run(entry);
    REQUIRE(receiver_woken);
}

TEST_CASE("channel: close wakes suspended sender with Closed error")
{
    bool sender_woken = false;

    auto entry = [&]() -> Task<> {
        Channel<int> ch{0};

        auto sender = [&]() -> Task<> {
            auto result = co_await ch.send(99);
            REQUIRE(!result);
            REQUIRE(result.error() == make_error_code(ChannelError::Closed));
            sender_woken = true;
        };

        auto closer = [&]() -> Task<> {
            co_await sleep_for(10ms);
            ch.close();
        };

        co_await all(sender(), closer());
    };

    run(entry);
    REQUIRE(sender_woken);
}

TEST_CASE("channel: receive drains buffer then returns Closed error")
{
    std::vector<int> out;

    auto entry = [&]() -> Task<> {
        Channel<int> ch{3};

        auto producer = [&]() -> Task<> {
            for (int i = 0; i < 3; ++i) {
                auto result = co_await ch.send(i);
                REQUIRE(result);
            }
            ch.close();
        };

        auto consumer = [&]() -> Task<> {
            co_await drain_channel(ch, out);
            auto final = co_await ch.receive();
            REQUIRE(!final);  // Should get Closed error
        };

        co_await all(producer(), consumer());
    };

    run(entry);
    REQUIRE(out == std::vector<int>{0, 1, 2});
}

TEST_CASE("channel: send to closed channel returns Closed error")
{
    auto entry = []() -> Task<> {
        Channel<int> ch{2};

        ch.close();
        auto result = co_await ch.send(42);
        REQUIRE(!result);
        REQUIRE(result.error() == make_error_code(ChannelError::Closed));
    };

    run(entry);
}

TEST_CASE("channel: multiple producers fan-in to single consumer")
{
    std::vector<int> out;

    auto entry = [&]() -> Task<> {
        Channel<int> ch{10};
        std::atomic<int> completed_producers{0};

        auto producer = [&](int id) -> Task<> {
            for (int i = 0; i < 3; ++i) {
                auto result = co_await ch.send(id * 100 + i);
                REQUIRE(result);
            }
            if (++completed_producers == 3) {
                ch.close();
            }
        };

        auto consumer = [&]() -> Task<> {
            co_await drain_channel(ch, out);
        };

        co_await all(producer(0), producer(1), producer(2), consumer());
    };

    run(entry);
    REQUIRE(out.size() == 9);
    std::sort(out.begin(), out.end());
    REQUIRE(out == std::vector<int>{0, 1, 2, 100, 101, 102, 200, 201, 202});
}
