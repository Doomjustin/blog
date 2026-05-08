#include <atomic>
#include <cstdlib>
#include <thread>

#include <blog.h>
#include <async/shift_to.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 场景 1：对称多核并发 (Symmetric Multi-Processing) 
// ============================================================================
auto demo_symmetric_workers(int worker_id) -> async::Task<>
{
    // 1️⃣ 这里的局部状态是绝对线程安全的，不需要 std::atomic 也不需要锁！
    // 因为这个协程在整个生命周期内，都被死死地钉在当前的物理线程上。
    int local_counter = 0; 
    
    log::info("[Worker {}] 启动于物理线程 {}", worker_id, std::this_thread::get_id());
    
    for (int i = 0; i < 3; ++i) {
        local_counter++;
        co_await async::sleep_for(100ms);
    }
    
    log::info("[Worker {}] 执行完毕，无锁累加结果: {}", worker_id, local_counter);
}

// ============================================================================
// 场景 2：跨线程的上下文切换 (Asymmetric Offloading)
// ============================================================================
auto demo_cross_thread_shift(async::IOContext& worker_ctx) -> async::Task<>
{
    log::info("\n=== Demo 2: 跨线程调度 (shift_to) ===");
    
    // 1️⃣ 记录当前的主线程 (Net) 上下文
    auto& net_ctx = async::this_coroutine::context();
    log::info("[Net] 收到用户请求，准备解析，当前线程: {}", std::this_thread::get_id());

    // 2️⃣ 绝杀：直接将当前协程的执行权“传送”到 Worker 线程！
    co_await async::shift_to(worker_ctx);
    
    log::info("[Worker] 执行重度 CPU 渲染任务，当前线程: {}", std::this_thread::get_id());
    co_await async::sleep_for(200ms); // 模拟吃 CPU 的耗时计算

    // 3️⃣ 计算完毕，切回 Net 线程进行网络发送
    co_await async::shift_to(net_ctx);
    
    log::info("[Net] 渲染完毕，通过网卡下发给客户端，当前线程: {}", std::this_thread::get_id());
    
    // 通知外部环境测试结束
    async::post(worker_ctx, [&worker_ctx] {
        worker_ctx.drop_work();
    });
}

} // namespace

int main()
{
    log::info("=== Demo 1: 对称多核架构 (async::run 启动 4 线程) ===");
    
    // 自动产生 4 个物理线程，各自绑定独立的 io_uring 事件循环
    std::atomic<int> worker_id_allocator{1};
    
    async::run(4, [&]() -> async::Task<> {
        int id = worker_id_allocator.fetch_add(1);
        co_await demo_symmetric_workers(id);
    });

    log::info("------------------------------------------------------");
    
    // 手动拉起两个不同角色的上下文，演示场景 2
    async::IOContext net_ctx;
    async::IOContext worker_ctx;

    worker_ctx.add_work();
    // 让 Worker 上下文在一个独立的后台物理线程跑起来
    std::jthread worker_thread([&] { worker_ctx.run(); });

    // 把任务丢给 Net 线程
    async::co_spawn(demo_cross_thread_shift(worker_ctx), net_ctx);
    
    // 主线程作为 Net 线程开始轮转
    net_ctx.run();

    return EXIT_SUCCESS;
}