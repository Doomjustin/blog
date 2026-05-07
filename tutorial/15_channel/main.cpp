#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

// ─── Demo 1：缓冲 channel，生产者-消费者 ────────────────────────────────────

auto producer(async::Channel<int>& ch) -> async::Task<>
{
    for (int i = 1; i <= 5; ++i) {
        log::info("send: {}", i);
        co_await ch.send(i);
    }
    ch.close();
    log::info("channel closed");
}

auto consumer(async::Channel<int>& ch) -> async::Task<>
{
    while (true) {
        auto result = co_await ch.receive();
        if (!result) {
            // ChannelError::Closed 表示 channel 已关闭且缓冲区已排空
            log::info("channel drained: {}", result.error());
            break;
        }
        log::info("recv: {}", *result);
    }
}

auto demo_producer_consumer() -> async::Task<>
{
    log::info("=== demo 1: producer-consumer (capacity=4) ===");
    async::Channel<int> ch{ 4 }; // 缓冲容量 4

    co_await async::all(
        producer(ch),
        consumer(ch)
    );
}

// ─── Demo 2：Rendezvous（capacity=0），send 与 receive 互相等待 ────────────

auto demo_rendezvous() -> async::Task<>
{
    log::info("\n=== demo 2: rendezvous (capacity=0) ===");
    async::Channel<std::string> ch{ 0 }; // unbuffered

    co_await async::all(
        [&]() -> async::Task<> {
            // sender 挂起，直到有 receiver 就绪
            log::info("sender: waiting for receiver...");
            auto result = co_await ch.send("hello");
            if (result)
                log::info("sender: value handed off");
        }(),
        [&]() -> async::Task<> {
            // receiver 挂起，直到有 sender 就绪
            log::info("receiver: waiting for sender...");
            auto result = co_await ch.receive();
            if (result)
                log::info("receiver: got \"{}\"", *result);
        }()
    );
}

// ─── Demo 3：Fan-in，多个 producer 汇聚到单个 consumer ──────────────────────

auto fan_in_producer(async::Channel<int>& ch, int id, int count) -> async::Task<>
{
    for (int i = 0; i < count; ++i) {
        auto value = id * 10 + i;
        co_await ch.send(value);
        log::info("producer {}: sent {}", id, value);
    }
}

auto demo_fan_in() -> async::Task<>
{
    log::info("\n=== demo 3: fan-in (3 producers → 1 consumer) ===");
    async::Channel<int> ch{ 8 };
    int total = 0;

    co_await async::all(
        // 三个 producer 并发写入同一个 channel
        fan_in_producer(ch, 1, 3),
        fan_in_producer(ch, 2, 3),
        fan_in_producer(ch, 3, 3),
        // 单个 consumer 读取所有值（producer 写完后关闭 channel）
        [&]() -> async::Task<> {
            // 等三个 producer 都结束后关闭 channel
            // 这里用 sleep 简单等待——实际项目中应用 Scope 或引用计数
            co_await async::sleep_for(50ms);
            ch.close();
        }(),
        [&]() -> async::Task<> {
            while (true) {
                auto result = co_await ch.receive();
                if (!result) break;
                total += *result;
                log::info("consumer: received {}", *result);
            }
            log::info("consumer: total = {}", total);
        }()
    );
}

// ─── Demo 4：timeout 与 channel 组合 ────────────────────────────────────────

auto demo_timeout() -> async::Task<>
{
    log::info("\n=== demo 4: timeout on channel receive ===");
    async::Channel<int> ch{ 0 }; // 无 sender，receive 会挂起

    auto result = co_await async::timeout(ch.receive(), 100ms);

    if (!result && result.error() == std::errc::timed_out) {
        log::info("receive timed out after 100ms (expected)");
    } else if (result && *result) {
        // result: expected<expected<int,ec>, ec>
        // *result: expected<int,ec>; **result: int
        log::info("received: {}", **result);
    }
}

} // namespace

int main()
{
    async::run(demo_producer_consumer);
    async::run(demo_rendezvous);
    async::run(demo_fan_in);
    async::run(demo_timeout);
    return EXIT_SUCCESS;
}
