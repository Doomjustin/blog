#include <chrono>
#include <cstdlib>
#include <thread>

// 引入我们的 TPC 框架
#include <asio.hpp>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

constexpr int MESSAGE_COUNT = 1'000'000;

// ============================================================================
// 选手 A：Boost.Asio (基于 io_context::post 的跨线程任务投递)
// 工业界最权威的异步框架，内部通过自旋锁 + 动态/缓存分配器实现事件队列。
// ============================================================================
auto run_asio_benchmark() -> void
{
    log::info("=== 选手 A: Boost.Asio (io_context::post) ===");
    
    asio::io_context ctx;
    auto work_guard = asio::make_work_guard(ctx);

    // 消费者：在后台物理线程运行 Asio 的事件循环
    std::thread consumer_thread([&ctx]() {
        ctx.run();
    });

    auto start = std::chrono::high_resolution_clock::now();

    // 生产者：在主线程疯狂向 Asio 事件循环投递 100 万个闭包任务
    for (int i = 0; i < MESSAGE_COUNT; ++i) {
        asio::post(ctx, [i]() {
            volatile int v = i; // 模拟消费动作，防止被优化
            (void)v;
        });
    }

    // 投递“毒丸”任务：取消工作守卫，使得处理完队列后事件循环退出
    asio::post(ctx, [&work_guard]() {
        work_guard.reset();
    });

    consumer_thread.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    log::warning("[Boost.Asio] 发送 {} 条跨线程消息，总耗时: {} ms", MESSAGE_COUNT, ms);
}

// ============================================================================
// 选手 B：我们的 TPC 框架 (基于 ChannelPipe + io_uring 无锁 MPSC 队列)
// 极致榨干硬件性能：节点池预分配 + 批量信用回归 + io_uring 事件驱动
// ============================================================================
auto run_tpc_channel_benchmark() -> void
{
    log::info("\n=== 选手 B: async::ChannelPipe (TPC 无锁架构) ===");
    
    async::IOContext ctx_net;
    async::IOContext ctx_worker;

    // 容量设为与 Asio 隐式队列相当的高吞吐模式
    auto [tx, rx] = async::make_channel<int>(8192, ctx_net, ctx_worker);

    auto start = std::chrono::high_resolution_clock::now();

    // 消费者协程 (跑在 Worker 线程)
    auto consumer_coro = [](async::ChannelReceiver<int> rx) -> async::Task<> {
        int count = 0;
        while (auto val = co_await rx.receive()) {
            volatile int v = *val;
            (void)v;
            if (++count == MESSAGE_COUNT) break;
        }
    };

    // 生产者协程 (跑在 Net 线程)
    auto producer_coro = [](async::ChannelSender<int> tx) -> async::Task<> {
        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            co_await tx.send(i);
        }
        tx.close();
    };

    // 保护 Worker 的事件循环防瞬间死亡
    ctx_worker.add_work();

    // 启动双核物理线程引擎
    async::co_spawn(consumer_coro(std::move(rx)), ctx_worker);
    std::jthread worker_thread([&ctx_worker] { 
        ctx_worker.run(); 
    });

    async::co_spawn(producer_coro(std::move(tx)), ctx_net);
    
    // 发射一个幽灵协程来监听完成状态并触发停机
    async::co_spawn([&ctx_worker]() -> async::Task<> {
        co_await async::sleep_for(10ms); // 等待所有数据跑完
        ctx_worker.drop_work();
    }(), ctx_net);

    ctx_net.run();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    log::info("[TPC 协程模型] 发送 {} 条跨线程消息，总耗时: {} ms", MESSAGE_COUNT, ms);
}

} // namespace

int main()
{
    log::info("🚀 开始与工业界标杆进行吞吐量极限测试...");
    
    run_asio_benchmark();
    run_tpc_channel_benchmark();
    
    return EXIT_SUCCESS;
}