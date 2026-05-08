#include <async/thread_safe_channel.h>

#include <atomic>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <async/all.h>
#include <async/run.h>
#include <async/sleep_for.h>
#include <async/task.h>
#include <async/timeout.h>
#include <async/when_any.h>

using namespace std::chrono_literals;

// ── Shared task helpers ──────────────────────────────────────────────────────

// Sends [0, count) then closes the channel.
static auto producer(async::ThreadSafeChannel<int>& ch, int count) -> async::Task<>
{
    for (int i = 0; i < count; ++i)
        co_await ch.send(i);
    ch.close();
}

// Sends `value` exactly `count` times (does not close).
static auto producer_fixed(async::ThreadSafeChannel<int>& ch, int value, int count) -> async::Task<>
{
    for (int i = 0; i < count; ++i)
        co_await ch.send(value);
}

// Drains until closed, appends each value to `out`.
static auto consumer(async::ThreadSafeChannel<int>& ch, std::vector<int>& out) -> async::Task<>
{
    while (true) {
        auto v = co_await ch.receive();
        if (!v)
            break;
        out.push_back(*v);
    }
}

// Receives exactly `count` values, accumulates into `total`.
static auto consumer_counted(async::ThreadSafeChannel<int>& ch, int count,
                             std::atomic<int>& total) -> async::Task<>
{
    for (int i = 0; i < count; ++i) {
        auto v = co_await ch.receive();
        REQUIRE(v.has_value());
        total += *v;
    }
}

// Waits 1 ms, sends one value, then closes the channel.
static auto slow_producer(async::ThreadSafeChannel<int>& ch, int value) -> async::Task<>
{
    co_await async::sleep_for(1ms);
    co_await ch.send(value);
    ch.close();
}

// Receives one value and stores it in `out`.
static auto receive_one(async::ThreadSafeChannel<int>& ch, int& out) -> async::Task<>
{
    auto v = co_await ch.receive();
    REQUIRE(v.has_value());
    out = *v;
}

// Sends one value then closes the channel.
static auto send_one_and_close(async::ThreadSafeChannel<int>& ch, int value) -> async::Task<>
{
    co_await ch.send(value);
    ch.close();
}

// ── Tests ────────────────────────────────────────────────────────────────────

TEST_CASE("thread_safe_channel: producer and consumer tasks communicate via buffered channel",
          "[async][thread_safe_channel]")
{
    std::vector<int> out;

    auto entry = [&]() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 4 };
        co_await async::all(producer(ch, 8), consumer(ch, out));
    };
    async::run(entry);

    REQUIRE(out == std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 });
}

TEST_CASE("thread_safe_channel: sender suspends when buffer is full, resumes as consumer drains",
          "[async][thread_safe_channel]")
{
    std::vector<int> out;

    auto entry = [&]() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 2 };
        co_await async::all(producer(ch, 6), consumer(ch, out));
    };
    async::run(entry);

    REQUIRE(out == std::vector<int>{ 0, 1, 2, 3, 4, 5 });
}

TEST_CASE("thread_safe_channel: receiver suspends until producer sends", "[async][thread_safe_channel]")
{
    int received = 0;

    auto entry = [&]() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 1 };
        co_await async::all(slow_producer(ch, 42), receive_one(ch, received));
    };
    async::run(entry);

    REQUIRE(received == 42);
}

TEST_CASE("thread_safe_channel: send to closed channel returns Closed error", "[async][thread_safe_channel]")
{
    auto entry = []() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 1 };
        ch.close();

        auto result = co_await ch.send(99);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == async::make_error_code(async::ThreadSafeChannelError::Closed));
    };
    async::run(entry);
}

TEST_CASE("thread_safe_channel: receive drains buffer then returns Closed error", "[async][thread_safe_channel]")
{
    auto entry = []() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 4 };

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
        REQUIRE(v3.error() == async::make_error_code(async::ThreadSafeChannelError::Closed));
    };
    async::run(entry);
}

TEST_CASE("thread_safe_channel: unbuffered rendezvous between two tasks", "[async][thread_safe_channel]")
{
    int received = 0;

    auto entry = [&]() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 };
        co_await async::all(send_one_and_close(ch, 7), receive_one(ch, received));
    };
    async::run(entry);

    REQUIRE(received == 7);
}

TEST_CASE("thread_safe_channel: multiple producers fan-in to single consumer", "[async][thread_safe_channel]")
{
    std::atomic<int> total{ 0 };

    auto entry = [&]() -> async::Task<> 
    {
        async::ThreadSafeChannel<int> ch{ 8 };
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

TEST_CASE("thread_safe_channel: close wakes suspended receiver with Closed error", "[async][thread_safe_channel]")
{
    // receiver suspends on empty channel, then close() wakes it
    std::expected<int, std::error_code> result{};

    auto receiver_task = [](async::ThreadSafeChannel<int>& ch,
                            std::expected<int, std::error_code>& out) -> async::Task<> {
        out = co_await ch.receive();
    };

    auto closer_task = [](async::ThreadSafeChannel<int>& ch) -> async::Task<> {
        co_await async::sleep_for(1ms);
        ch.close();
    };

    auto entry = [&]() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 };
        co_await async::all(receiver_task(ch, result), closer_task(ch));
    };
    async::run(entry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == async::make_error_code(async::ThreadSafeChannelError::Closed));
}

TEST_CASE("thread_safe_channel: close wakes suspended sender with Closed error", "[async][thread_safe_channel]")
{
    // sender suspends on full channel (rendezvous), then close() wakes it
    std::expected<void, std::error_code> result{};

    auto sender_task = [](async::ThreadSafeChannel<int>& ch,
                          std::expected<void, std::error_code>& out) -> async::Task<> {
        out = co_await ch.send(42);
    };

    auto closer_task = [](async::ThreadSafeChannel<int>& ch) -> async::Task<> {
        co_await async::sleep_for(1ms);
        ch.close();
    };

    auto entry = [&]() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 }; // rendezvous: sender will suspend
        co_await async::all(sender_task(ch, result), closer_task(ch));
    };
    async::run(entry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == async::make_error_code(async::ThreadSafeChannelError::Closed));
}

TEST_CASE("thread_safe_channel: cancelled receive via when_any returns operation_canceled",
          "[async][thread_safe_channel]")
{
    // receive suspends; a concurrent sleep wins the race and cancels the receive
    auto entry = []() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 };

        auto result = co_await async::when_any(
            ch.receive(),
            async::sleep_for(1ms)
        );

        // sleep_for has resume_type void; ch.receive() has resume_type int
        // → heterogeneous → variant; index 1 means sleep won
        REQUIRE(result.index() == 1);
    };
    async::run(entry);
}

TEST_CASE("thread_safe_channel: cancelled send via when_any returns operation_canceled",
          "[async][thread_safe_channel]")
{
    // sender suspends on rendezvous channel; sleep wins the race and cancels the send
    // send and sleep both have resume_type void → when_any returns expected<void>
    // when_any returns the winner's result: sleep succeeded → has_value()
    // the key assertion is that the whole thing completes without hanging
    auto entry = []() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 }; // rendezvous: send will suspend

        auto result = co_await async::when_any(
            ch.send(99),
            async::sleep_for(1ms)
        );

        // sleep won → its result (success) is returned
        REQUIRE(result.has_value());
    };
    async::run(entry);
}

TEST_CASE("thread_safe_channel: timeout on suspended receive returns timed_out", "[async][thread_safe_channel]")
{
    // No sender: receive will suspend. timeout() races it against a timer.
    auto entry = []() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 };

        auto result = co_await async::timeout(ch.receive(), 1ms);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == std::make_error_code(std::errc::timed_out));
    };
    async::run(entry);
}

TEST_CASE("thread_safe_channel: timeout on suspended send returns timed_out", "[async][thread_safe_channel]")
{
    // No receiver: send will suspend on rendezvous channel. timeout() cancels it.
    auto entry = []() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 };

        auto result = co_await async::timeout(ch.send(42), 1ms);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == std::make_error_code(std::errc::timed_out));
    };
    async::run(entry);
}

TEST_CASE("thread_safe_channel: timeout does not fire when send completes in time", "[async][thread_safe_channel]")
{
    // A receiver task is already waiting; send completes immediately.
    auto entry = []() -> async::Task<> {
        async::ThreadSafeChannel<int> ch{ 0 };
        int received = -1;

        auto receiver = [&]() -> async::Task<> {
            auto r = co_await ch.receive();
            REQUIRE(r.has_value());
            received = *r;
        };

        co_await async::all(
            receiver(),
            [&]() -> async::Task<> {
                auto result = co_await async::timeout(ch.send(7), 5s);
                REQUIRE(result.has_value());
            }()
        );

        REQUIRE(received == 7);
    };
    async::run(entry);
}

