#include <cstdlib>
#include <string>
#include <thread>

#include <blog.h>
#include <async/channel_pipe.h>
#include <async/sleep_for.h>

using namespace std::chrono_literals;

namespace {

// 消费者：模拟后台计算线程
auto worker_task(async::ChannelReceiver<std::string> rx) -> async::Task<>
{
    log::info("[Worker] started, waiting for requests...");

    // 优雅停机循环：发送端 close 且通道内积压的数据被完全抽干后，
    // receive() 返回 Closed 错误，循环自然退出。
    while (auto task_data = co_await rx.receive()) {
        log::info("[Worker] processing: {}", *task_data);

        // 模拟繁重的 CPU 计算 (200ms)
        co_await async::sleep_for(200ms);

        log::info("[Worker] done: {}", *task_data);
    }

    log::info("[Worker] pipe closed, all pending tasks drained, exiting.");
}

// 生产者：模拟网络 I/O 线程
auto net_task(async::ChannelSender<std::string> tx) -> async::Task<>
{
    log::info("[Net] started, receiving frontend requests...");

    for (int i = 1; i <= 6; ++i) {
        std::string data = "Request-" + std::to_string(i);
        log::info("[Net] received {}, forwarding to worker...", data);

        // 管道（容量 4）满时 send 挂起当前协程，但 Net 线程的 io_uring 继续运行。
        auto result = co_await tx.send(data);
        if (!result) {
            log::error("[Net] send failed, pipe closed unexpectedly!");
            break;
        }

        // 模拟高频收包节拍 (50ms)，生产速度远高于消费速度
        co_await async::sleep_for(50ms);
    }

    log::info("[Net] all requests dispatched, closing sender.");
    tx.close();
}

auto demo_cross_thread_pipeline() -> void
{
    log::info("=== Demo: cross-thread pipeline with backpressure ===");

    async::IOContext ctx_net;    // Thread A: 网络 IO 上下文
    async::IOContext ctx_worker; // Thread B: 密集计算上下文

    // 容量为 4 的跨线程有界管道（自带背压）
    auto [tx, rx] = async::make_channel<std::string>(4, ctx_net, ctx_worker);

    // 将 Worker 协程部署到 ctx_worker，由独立的 OS 线程驱动
    async::co_spawn(worker_task(std::move(rx)), ctx_worker);
    std::jthread worker_thread([&ctx_worker] { ctx_worker.run(); });

    // 将 Net 协程部署到 ctx_net，在主线程驱动
    async::co_spawn(net_task(std::move(tx)), ctx_net);
    ctx_net.run();

    // ctx_net.run() 返回后，jthread RAII 自动 join，等待 Worker 退出
}

} // namespace

int main()
{
    demo_cross_thread_pipeline();
    return EXIT_SUCCESS;
}
