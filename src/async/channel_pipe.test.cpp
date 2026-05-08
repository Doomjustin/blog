#include <async/channel_pipe.h>

#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <async/co_spawn.h>
#include <async/run.h>
#include <async/sleep_for.h>
#include <async/task.h>
#include <async/timeout.h>

using namespace std::chrono_literals;
using namespace async;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Run a sender coroutine on ctx_a and a receiver coroutine on ctx_b.
// ctx_a and ctx_b must already exist when this is called.
// The receiver is co_spawn'd first (from main thread, before ctx_b.run()),
// then ctx_b is started in a background jthread.
// The sender is co_spawn'd onto ctx_a and ctx_a.run() blocks until done.
template<typename SenderFn, typename ReceiverFn>
static void run_pipeline(IOContext& ctx_a, IOContext& ctx_b,
                         SenderFn&& sender_fn, ReceiverFn&& receiver_fn)
{
    // Spawn receiver — runs synchronously on this thread until first suspension,
    // then continued by ctx_b's event loop.
    co_spawn(std::forward<ReceiverFn>(receiver_fn)(), ctx_b);

    std::jthread thread_b([&ctx_b] { ctx_b.run(); });

    // Spawn and run sender on ctx_a (this thread acts as Thread A).
    co_spawn(std::forward<SenderFn>(sender_fn)(), ctx_a);
    ctx_a.run();

    // thread_b joins here; ctx_b.run() exits once receiver completes.
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_CASE("channel_pipe: error category and type traits", "[async][channel_pipe]")
{
    auto err = make_error_code(ChannelPipeError::Closed);
    REQUIRE(err.category().name() == std::string("channel_pipe"));
    REQUIRE(err.value() == 1);

    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(4, ctx_a, ctx_b);

    static_assert(std::is_same_v<decltype(tx.send(0))::resume_type, void>);
    static_assert(std::is_same_v<decltype(rx.receive())::resume_type,
                                 std::expected<int, std::error_code>>);

    // Awaiters must satisfy cancelable_operation so they compose with timeout/when_any.
    static_assert(cancelable_operation<decltype(tx.send(0))>);
    static_assert(cancelable_operation<decltype(rx.receive())>);
}

TEST_CASE("channel_pipe: send and receive across two IOContexts", "[async][channel_pipe]")
{
    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(8, ctx_a, ctx_b);
    std::vector<int> received;

    auto sender = [tx = std::move(tx), &ctx_a]() mutable -> Task<> {
        for (int i = 0; i < 5; ++i) {
            auto result = co_await tx.send(i);
            REQUIRE(result);
        }
        tx.close();
    };

    auto receiver = [rx = std::move(rx), &received]() mutable -> Task<> {
        while (auto v = co_await rx.receive())
            received.push_back(*v);
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));

    REQUIRE(received == std::vector<int>{ 0, 1, 2, 3, 4 });
}

TEST_CASE("channel_pipe: backpressure — sender suspends when credits exhausted",
          "[async][channel_pipe]")
{
    // capacity=4, batch=1 (4/4). Sender must be granted credits
    // for every message beyond the initial 4.
    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(4, ctx_a, ctx_b);
    std::vector<int> received;

    auto sender = [tx = std::move(tx), &ctx_a]() mutable -> Task<> {
        for (int i = 0; i < 12; ++i) {
            auto result = co_await tx.send(i);
            REQUIRE(result);
        }
        tx.close();
    };

    auto receiver = [rx = std::move(rx), &received]() mutable -> Task<> {
        while (auto v = co_await rx.receive())
            received.push_back(*v);
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));

    REQUIRE(received.size() == 12);
    for (int i = 0; i < 12; ++i)
        REQUIRE(received[i] == i);
}

TEST_CASE("channel_pipe: receiver gets Closed after sender closes and buffer is drained",
          "[async][channel_pipe]")
{
    IOContext ctx_a;
    IOContext ctx_b;
    // capacity=4, send 3 (< capacity, no credit block), then close.
    auto [tx, rx] = make_channel<int>(4, ctx_a, ctx_b);
    std::vector<int> received;
    bool saw_closed = false;

    auto sender = [tx = std::move(tx)]() mutable -> Task<> {
        for (int i = 0; i < 3; ++i)
            co_await tx.send(i);
        tx.close();
    };

    auto receiver = [rx = std::move(rx), &received, &saw_closed]() mutable -> Task<> {
        while (auto v = co_await rx.receive())
            received.push_back(*v);
        saw_closed = true;
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));

    REQUIRE(received == std::vector<int>{ 0, 1, 2 });
    REQUIRE(saw_closed);
}

TEST_CASE("channel_pipe: receiver close propagates Closed error to pending sender",
          "[async][channel_pipe]")
{
    // capacity=0 is not valid; use capacity=1.
    // Receiver closes early; sender should get Closed on the next blocked send.
    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(1, ctx_a, ctx_b);
    bool sender_got_closed = false;

    auto sender = [tx = std::move(tx), &sender_got_closed]() mutable -> Task<> {
        // First send: fast path (1 credit available).
        auto r1 = co_await tx.send(100);
        REQUIRE(r1);
        // Second send: credits exhausted — will suspend. Receiver will close
        // while this send is parked.
        auto r2 = co_await tx.send(200);
        REQUIRE(!r2);
        REQUIRE(r2.error() == make_error_code(ChannelPipeError::Closed));
        sender_got_closed = true;
    };

    auto receiver = [rx = std::move(rx), &ctx_b]() mutable -> Task<> {
        // Receive the first value, then close without consuming more.
        auto v = co_await rx.receive();
        REQUIRE(v);
        REQUIRE(*v == 100);
        rx.close();
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));

    REQUIRE(sender_got_closed);
}

TEST_CASE("channel_pipe: send to already-closed channel returns Closed immediately",
          "[async][channel_pipe]")
{
    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(4, ctx_a, ctx_b);

    // Receiver: receive one value then close.
    auto receiver = [rx = std::move(rx), &ctx_b]() mutable -> Task<> {
        co_await rx.receive(); // drain one
        rx.close();
    };

    auto sender = [tx = std::move(tx)]() mutable -> Task<> {
        // Close sender immediately, before any sends.
        tx.close();
        // Subsequent sends must fail.
        auto result = co_await tx.send(42);
        REQUIRE(!result);
        REQUIRE(result.error() == make_error_code(ChannelPipeError::Closed));
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));
}

TEST_CASE("channel_pipe: large transfer — 1000 items through capacity-16 channel",
          "[async][channel_pipe]")
{
    IOContext ctx_a;
    IOContext ctx_b;
    constexpr int N = 1000;
    auto [tx, rx] = make_channel<int>(16, ctx_a, ctx_b);
    std::vector<int> received;
    received.reserve(N);

    auto sender = [tx = std::move(tx)]() mutable -> Task<> {
        for (int i = 0; i < N; ++i) {
            auto result = co_await tx.send(i);
            REQUIRE(result);
        }
        tx.close();
    };

    auto receiver = [rx = std::move(rx), &received]() mutable -> Task<> {
        while (auto v = co_await rx.receive())
            received.push_back(*v);
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));

    REQUIRE(received.size() == static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i)
        REQUIRE(received[i] == i);
}

TEST_CASE("channel_pipe: send cancel via timeout returns operation_canceled",
          "[async][channel_pipe]")
{
    // capacity=1 so the second send immediately exhausts credits and suspends.
    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(1, ctx_a, ctx_b);
    std::error_code ec{};

    auto sender = [tx = std::move(tx), &ec]() mutable -> Task<> {
        // First send consumes the only credit — succeeds.
        auto r1 = co_await tx.send(1);
        REQUIRE(r1);
        // Second send has no credits; wrap with a tight timeout so it is cancelled.
        auto r2 = co_await timeout(tx.send(2), 1ms);
        REQUIRE(!r2);
        ec = r2.error();
        // Close so the receiver side can exit cleanly.
        tx.close();
    };

    auto receiver = [rx = std::move(rx)]() mutable -> Task<> {
        // Deliberately slow: sleep before consuming, giving the timeout time to fire.
        co_await sleep_for(50ms);
        while (auto v = co_await rx.receive())
            ;
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));

    REQUIRE(ec == std::errc::timed_out);
}

TEST_CASE("channel_pipe: receive cancel via timeout returns operation_canceled",
          "[async][channel_pipe]")
{
    IOContext ctx_a;
    IOContext ctx_b;
    auto [tx, rx] = make_channel<int>(4, ctx_a, ctx_b);
    std::error_code ec{};

    auto receiver = [rx = std::move(rx), &ec]() mutable -> Task<> {
        // Channel is empty; wrap receive with a tight timeout.
        auto r = co_await timeout(rx.receive(), 1ms);
        REQUIRE(!r);
        ec = r.error();
    };

    auto sender = [tx = std::move(tx)]() mutable -> Task<> {
        // Wait long enough for the receiver timeout to fire before closing.
        co_await sleep_for(50ms);
        tx.close();
    };

    run_pipeline(ctx_a, ctx_b, std::move(sender), std::move(receiver));

    REQUIRE(ec == std::errc::timed_out);
}
