#include <chrono>
#include <cstdlib>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 场景 1：全双工并发 I/O (Full-Duplex I/O)
// 在网关或代理服务器中，我们经常需要同时向后端发送数据，并接收前端的新数据。
// when_all 在这里是完美的，因为它绝对 0 内存分配，直接在底层投递两个 SQE。
// ============================================================================
auto demo_full_duplex() -> async::Task<>
{
    using namespace std::chrono;

    log::info("=== Demo 1: 全双工并发 I/O (零内存分配) ===");

    // 真实场景下这里会是 net::send(fd_out) 和 net::receive(fd_in)
    // 我们用 sleep_for 模拟底层的 I/O 等待时间
    auto start = steady_clock::now();

    log::info("[Proxy] 正在同时发起网络转发与接收...");

    auto [send_res, recv_res] = co_await async::when_all(
        async::sleep_for(50ms),   // 模拟发送数据到后端的网络耗时
        async::sleep_for(150ms)   // 模拟等待前端下一批数据的耗时
    );

    auto ms = duration_cast<milliseconds>(steady_clock::now() - start);

    log::info("[Proxy] 全双工 I/O 结束，总耗时: {}ms (预期受限于最慢的 ~150ms)", ms.count());

    // 独立检查每一条 I/O 链路的健康状态
    if (send_res && recv_res)
        log::info("[Proxy] 数据转发与接收均成功！链路保持活跃。");
    else
        log::error("[Proxy] 链路发生异常中断。");
}

// ============================================================================
// 场景 2：带 SLA 熔断的 Scatter-Gather (并发拉取与局部降级)
// 业务需要聚合 UserDB(核心) 和 ThirdPartyAPI(边缘)。
// ThirdPartyAPI 如果超过 200ms 不返回，就直接熔断，但绝不能影响 UserDB 的查询。
// ============================================================================
auto demo_sla_scatter_gather() -> async::Task<>
{
    log::info("\n=== Demo 2: 带 SLA 熔断的并发拉取 (Partial Failure) ===");

    log::info("[Gateway] 开始并发请求 UserDB 和 ThirdPartyAPI...");

    // 将不稳定的第三方调用包裹在 timeout 组合子里，与稳定的 DB 请求一起投递
    auto [db_res, api_res] = co_await async::when_all(
        async::sleep_for(100ms),                      // 模拟 UserDB 稳定且快速返回
        async::timeout(async::sleep_for(5s), 200ms)  // 模拟第三方 API 卡死，200ms 强制熔断
    );

    // 1. 处理核心数据 (必须成功)
    if (db_res) {
        log::info("[Gateway] UserDB 核心数据拉取成功！");
    } else {
        log::error("[Gateway] UserDB 拉取失败，触发致命级业务错误！");
        co_return;
    }

    // 2. 处理边缘数据 (允许服务降级)
    if (api_res)
        log::info("[Gateway] ThirdPartyAPI 数据拉取成功！");
    else if (api_res.error() == std::errc::timed_out)
        log::warning("[Gateway] ThirdPartyAPI 响应超时 (已熔断)，对该模块进行缓存降级处理。");
    else
        log::error("[Gateway] ThirdPartyAPI 发生其他底层错误: {}", api_res.error());
}

// ============================================================================
// 场景 3：高可用并发双写 (Quorum Write)
// 将重要日志同时写入 Primary Node 和 Backup Node。
// 我们需要等待两者的物理 I/O 都结束，只要有一个成功，业务就认为落盘成功。
// ============================================================================
auto demo_quorum_write() -> async::Task<>
{
    log::info("\n=== Demo 3: 高可用并发双写 (Quorum Write) ===");

    log::info("[Storage] 正在将区块数据并发刷入主备节点...");

    auto [primary_res, backup_res] = co_await async::when_all(
        async::sleep_for(80ms),   // Primary 节点网络良好，迅速落盘
        async::sleep_for(500ms)   // Backup 节点磁盘抖动，写入极慢
    );

    // 等待全部完成后的多数派决议 (Quorum Consensus)
    if (primary_res && backup_res)
        log::info("[Storage] 主备节点均写入成功，达成强一致性 (Strong Consistency)！");
    else if (primary_res || backup_res)
        log::warning("[Storage] 仅单个节点写入成功，警报：系统降级为弱一致性。");
    else
        log::error("[Storage] 主备节点全部写入失败！存在极高数据丢失风险！");
}

} // namespace

int main()
{
    async::run(demo_full_duplex);
    async::run(demo_sla_scatter_gather);
    async::run(demo_quorum_write);
    return EXIT_SUCCESS;
}

