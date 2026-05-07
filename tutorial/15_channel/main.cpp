#include <cstdlib>
#include <format>
#include <string>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 场景 1：Fan-in 多路汇聚 (聊天室广播器)
// 多个客户端（生产者）并发写入，单个房间管理器（消费者）统一读取广播。
// 展示了如何使用 async::all 一行代码优雅地等待所有生产者结束并关闭通道。
// ============================================================================

auto chat_client(int client_id, async::Channel<std::string>& room_ch) -> async::Task<>
{
    for (int i = 1; i <= 3; ++i) {
        std::string msg = std::format("Client-{} 发送了弹幕 {}", client_id, i);
        log::info("[Client {}] {}", client_id, msg);

        co_await room_ch.send(msg);
        co_await async::sleep_for(20ms); // 模拟打字延迟
    }

    log::info("[Client {}] 离开聊天室", client_id);
}

auto room_manager(async::Channel<std::string>& room_ch) -> async::Task<>
{
    log::info("[Room] 房间广播器启动，等待弹幕...");

    // 安全消费：通道 close 且所有数据排空后，自动跳出循环
    while (auto msg = co_await room_ch.receive())
        log::info("[Room] >> 全服广播: {}", *msg);

    log::info("[Room] 通道已关闭，广播器安全退出。");
}

auto demo_fan_in() -> async::Task<>
{
    log::info("=== Demo 1: Fan-in 多路汇聚 (聊天室场景，容量 16) ===");
    async::Channel<std::string> room_ch{ 16 };

    // 1. 后台启动消费者（脱离当前协程的等待链，在后台独立循环）
    async::co_spawn(room_manager(room_ch));

    // 2. 结构化并发：使用 async::all 一行代码并发执行并等待所有生产者！
    // 相比手写 Scope，这里完全消除了中间状态，代码变得极其具有"表达式美感"。
    co_await async::all(
        chat_client(1, room_ch),
        chat_client(2, room_ch),
        chat_client(3, room_ch)
    );

    // 3. 当代码走到这里时，代表那 3 个客户端已经 100% 物理执行完毕了
    log::info("[Main] 所有客户端已下线，关闭房间通道。");
    room_ch.close(); // 安全关闭通道，触发 room_manager 退出

    co_await async::sleep_for(10ms); // 稍等片刻，让 room_manager 把遗言打印完
}

// ============================================================================
// 场景 2：Rendezvous 强交握 (间谍接头)
// Capacity = 0。没有缓冲，发送方必须死等接收方到位，反之亦然。
// ============================================================================

auto spy_delegator(async::Channel<std::string>& ch) -> async::Task<>
{
    log::info("[Delegator] 带着核心机密前往接头地点...");
    co_await async::sleep_for(50ms); // 模拟路上耽搁了

    log::info("[Delegator] 到达接头点，尝试移交机密 (挂起死等接头人)...");

    // 容量为 0，且接头人还没来，这里会强制挂起当前协程！
    auto result = co_await ch.send("TOP_SECRET_CODE");
    if (result)
        log::info("[Delegator] 机密已成功移交！撤退。");
}

auto spy_agent(async::Channel<std::string>& ch) -> async::Task<>
{
    log::info("[Agent] 提前到达接头地点，等待机密 (挂起死等)...");

    auto result = co_await ch.receive();
    if (result)
        log::info("[Agent] 拿到机密: {}，迅速撤离！", *result);

    ch.close();
}

auto demo_rendezvous() -> async::Task<>
{
    log::info("\n=== Demo 2: Rendezvous 零容量强交握 (间谍接头) ===");
    async::Channel<std::string> ch{ 0 };

    // 使用 async::all 并发执行两个强交握任务
    co_await async::all(spy_delegator(ch), spy_agent(ch));
}

// ============================================================================
// 场景 3：Channel + Timeout 熔断器 (看门狗)
// ============================================================================

auto demo_timeout() -> async::Task<>
{
    log::info("\n=== Demo 3: Timeout 看门狗熔断 ===");
    async::Channel<std::string> ch{ 4 };

    log::info("[Monitor] 等待心跳信号，最多等待 100ms...");

    // 将 channel 接收操作包在 timeout 里，这是极其高频的防御性编程写法
    auto result = co_await async::timeout(ch.receive(), 100ms);

    if (!result && result.error() == std::errc::timed_out)
        log::warning("[Monitor] 100ms 内未收到任何数据，触发熔断警报！(符合预期)");
    else
        log::error("[Monitor] 收到异常数据");
}

} // namespace

int main()
{
    async::run(demo_fan_in);
    async::run(demo_rendezvous);
    async::run(demo_timeout);
    return EXIT_SUCCESS;
}
