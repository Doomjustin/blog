#include <chrono>
#include <cstdlib>
#include <stop_token>
#include <string>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 场景 1：Task 级多路并发 (async::all)
// 业务场景：微服务数据聚合 (BFF 网关)。
// 拉取 UserDB、OrderDB、MessageMQ 三路数据。每一路都是
// [建立连接] -> [等待 I/O] -> [反序列化] 的多步状态机，
// 无法用单一的底层 I/O awaiter 表达——这正是 async::all 的适用场景。
// ============================================================================

auto fetch_and_process(std::string service_name, std::chrono::milliseconds latency) -> async::Task<>
{
    log::info("[{}] 1. 开始建立连接并发起 RPC 请求...", service_name);

    co_await async::sleep_for(latency);  // 模拟网络 I/O

    log::info("[{}] 2. 网络响应到达，开始进行 JSON 反序列化...", service_name);

    co_await async::sleep_for(10ms);     // 模拟 CPU 处理

    log::info("[{}] 3. 数据处理完毕！", service_name);
}

auto demo_task_all() -> async::Task<>
{
    using namespace std::chrono;

    log::info("=== Demo 1: Task 级多路并发 (微服务聚合 BFF) ===");
    auto start = steady_clock::now();

    co_await async::all(
        fetch_and_process("User-Service",    120ms),
        fetch_and_process("Order-Service",   400ms),
        fetch_and_process("Message-Service", 260ms)
    );

    auto ms = duration_cast<milliseconds>(steady_clock::now() - start).count();
    log::info("[Gateway] 所有微服务数据聚合完毕，总耗时: {}ms (预期约 410ms)。下发给前端。", ms);
}


// ============================================================================
// 场景 2：Task 级协作式取消 (async::any)
// 业务场景：投机执行 / 最快镜像源下载。
// 同时连接三个镜像源下载同一文件。最快的一个下载完成后，
// 通过 stop_token 安全地终止其余下载，防止继续占用带宽。
// ============================================================================

// stop_token 必须按值传入——async::task 会在运行时自动注入。
// 按引用传入会导致协程帧持有悬空引用（UB）。
auto download_from_mirror(
    std::string mirror_name,
    std::chrono::milliseconds chunk_delay,
    std::stop_token token
) -> async::Task<>
{
    log::info("[{}] 建立连接，准备分块下载文件...", mirror_name);

    for (int chunk = 1; chunk <= 5; ++chunk) {
        // stop_then 将 io_uring CANCEL SQE 与 stop_token 协同：
        // 一旦其他镜像率先完成，此处立即收到 operation_canceled。
        auto result = co_await async::stop_then(async::sleep_for(chunk_delay), token);

        if (!result && result.error() == std::errc::operation_canceled) {
            log::warning("[{}] 收到取消信号，放弃剩余下载，清理临时文件...", mirror_name);
            co_return;
        }

        log::info("[{}] 已下载区块 {}/5", mirror_name, chunk);
    }

    log::info("[{}] 下载完成！", mirror_name);
}

auto demo_task_any() -> async::Task<>
{
    using namespace std::chrono;
    
    log::info("\n=== Demo 2: Task 级协作式取消 (最快镜像源竞速) ===");
    auto start = steady_clock::now();

    co_await async::any(
        async::task(download_from_mirror, "阿里云镜像", 150ms),  // 慢
        async::task(download_from_mirror, "腾讯云镜像",  50ms),  // 快 (Winner)
        async::task(download_from_mirror, "清华源镜像",  200ms)  // 极慢
    );

    auto ms = duration_cast<milliseconds>(steady_clock::now() - start).count();
    log::info("[System] 文件下载任务完成，总耗时: {}ms。落败节点已被安全回收。", ms);
}

} // namespace

int main()
{
    async::run(demo_task_all);
    async::run(demo_task_any);
    return EXIT_SUCCESS;
}
