#include <channel.h>

#include <atomic>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <all.h>
#include <run.h>
#include <sleep_for.h>
#include <task.h>

using namespace std::chrono_literals;

// ── Shared task helpers ──────────────────────────────────────────────────────

// Sends [0, count) then closes the channel.
static auto producer(async::Channel<int>& ch, int count) -> async::Task<>
{
    for (int i = 0; i < count; ++i)
        co_await ch.send(i);
    ch.close();
}

// Sends `value` exactly `count` times (does not close).
static auto producer_fixed(async::Channel<int>& ch, int value, int count) -> async::Task<>
{
    for (int i = 0; i < count; ++i)
        co_await ch.send(value);
}

// Drains until closed, appends each value to `out`.
static auto consumer(async::Channel<int>& ch, std::vector<int>& out) -> async::Task<>
{
    while (true) {
        auto v = co_await ch.receive();
        if (!v)
            break;
        out.push_back(*v);
    }
}

// Receives exactly `count` values, accumulates into `total`.
static auto consumer_counted(async::Channel<int>& ch, int count,
                             std::atomic<int>& total) -> async::Task<>
{
    for (int i = 0; i < count; ++i) {
        auto v = co_await ch.receive();
        REQUIRE(v.has_value());
        total += *v;
    }
}

// Waits 1 ms, sends one value, then closes the channel.
static auto slow_producer(async::Channel<int>& ch, int value) -> async::Task<>
{
    co_await async::sleep_for(1ms);
    co_await ch.send(value);
    ch.close();
}

// Receives one value and stores it in `out`.
static auto receive_one(async::Channel<int>& ch, int& out) -> async::Task<>
{
    auto v = co_await ch.receive();
    REQUIRE(v.has_value());
    out = *v;
}

// Sends one value then closes the channel.
static auto send_one_and_close(async::Channel<int>& ch, int value) -> async::Task<>
{
    co_await ch.send(value);
    ch.close();
}

// ── Tests ────────────────────────────────────────────────────────────────────

TEST_CASE("channel: producer and consumer tasks communicate via buffered channel",
          "[async][channel]")
{
    std::vector<int> out;

    auto entry = [&]() -> async::Task<> {
        async::Channel<int> ch{ 4 };
        co_await async::all(producer(ch, 8), consumer(ch, out));
    };
    async::run(entry);

    REQUIRE(out == std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 });
}

TEST_CASE("channel: sender suspends when buffer is full, resumes as consumer drains",
          "[async][channel]")
{
    std::vector<int> out;

    auto entry = [&]() -> async::Task<> {
        async::Channel<int> ch{ 2 };
        co_await async::all(producer(ch, 6), consumer(ch, out));
    };
    async::run(entry);

    REQUIRE(out == std::vector<int>{ 0, 1, 2, 3, 4, 5 });
}

TEST_CASE("channel: receiver suspends until producer sends", "[async][channel]")
{
    int received = 0;

    auto entry = [&]() -> async::Task<> {
        async::Channel<int> ch{ 1 };
        co_await async::all(slow_producer(ch, 42), receive_one(ch, received));
    };
    async::run(entry);

    REQUIRE(received == 42);
}

TEST_CASE("channel: send to closed channel returns Closed error", "[async][channel]")
{
    auto entry = []() -> async::Task<> {
        async::Channel<int> ch{ 1 };
        ch.close();

        auto result = co_await ch.send(99);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == async::make_error_code(async::ChannelError::Closed));
    };
    async::run(entry);
}

TEST_CASE("channel: receive drains buffer then returns Closed error", "[async][channel]")
{
    auto entry = []() -> async::Task<> {
        async::Channel<int> ch{ 4 };

        co_await ch.send(10);
        co_await ch.send(20);
        ch.close();

        auto v1 = co_await ch.receive();
        REQUIRE(v1.has_value());
        REQUIRE(*v1 == 10);

        auto v2 = co_await ch.receive();
        REQUIRE(v2.has_value());
        REQUIRE(*v2 == 20);

        auto v3 = co_await ch.receive();
        REQUIRE_FALSE(v3.has_value());
        REQUIRE(v3.error() == async::make_error_code(async::ChannelError::Closed));
    };
    async::run(entry);
}

TEST_CASE("channel: unbuffered rendezvous between two tasks", "[async][channel]")
{
    int received = 0;

    auto entry = [&]() -> async::Task<> {
        async::Channel<int> ch{ 0 };
        co_await async::all(send_one_and_close(ch, 7), receive_one(ch, received));
    };
    async::run(entry);

    REQUIRE(received == 7);
}

TEST_CASE("channel: multiple producers fan-in to single consumer", "[async][channel]")
{
    std::atomic<int> total{ 0 };

    auto entry = [&]() -> async::Task<> 
    {
        async::Channel<int> ch{ 8 };
        // 3 producers × 3 values each → consumer reads 9 messages
        co_await async::all(
            producer_fixed(ch, 1,   3),
            producer_fixed(ch, 10,  3),
            producer_fixed(ch, 100, 3),
            consumer_counted(ch, 9, total)
        );
    };
    async::run(entry);

    // 3×1 + 3×10 + 3×100 = 333
    REQUIRE(total == 333);
}


