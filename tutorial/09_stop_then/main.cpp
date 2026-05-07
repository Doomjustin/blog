#include <cstdlib>
#include <stop_token>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ============================================================================
// 场景 1：用户主动取消长耗时 I/O（纯异步）
// ============================================================================
auto demo_user_cancellation() -> async::Task<>
{
    log::info("=== Demo 1: 纯异步协作式取消 (告别 OS 线程) ===");

    std::stop_source stop_src;

    // 后台协程在同一事件循环里延时触发 stop，不创建任何 OS 线程。
    async::co_spawn([](std::stop_source src) -> async::Task<> {
        co_await async::sleep_for(300ms);
        log::warning("[UI] 收到取消指令，触发 StopToken。");
        src.request_stop();
    }(stop_src));

    log::info("[Downloader] 开始下载大文件 (预计需要 10 秒)...");

    auto result = co_await async::stop_then(
        async::sleep_for(10s),
        stop_src.get_token()
    );

    if (!result && result.error() == std::errc::operation_canceled)
        log::warning("[Downloader] 下载被安全中断，底层 io_uring 已回收挂起操作。\n");
    else
        log::error("[Downloader] 未预期的结果。");
}

// ============================================================================
// 场景 2：核心循环优雅停机（Graceful Shutdown）
// ============================================================================
auto demo_server_loop() -> async::Task<>
{
    log::info("=== Demo 2: 核心循环的优雅停机 (Graceful Shutdown) ===");

    std::stop_source stop_src;

    async::co_spawn([](std::stop_source src) -> async::Task<> {
        co_await async::sleep_for(800ms);
        log::warning("[Admin] 下发平滑停机指令。");
        src.request_stop();
    }(stop_src));

    log::info("[Server] 开始监听新连接 (死循环)...");

    int conn_count = 0;
    while (true) {
        auto result = co_await async::stop_then(
            async::sleep_for(200ms),
            stop_src.get_token()
        );

        if (!result) {
            if (result.error() == std::errc::operation_canceled) {
                log::info("[Server] 收到停机指令，阻塞等待已被打断。");
                log::info("[Server] 拒绝新请求并等待旧请求排空...");
                break;
            }
            
            log::error("[Server] 等待连接时出现错误: {}", result.error());
            break;
        }

        log::info("[Server] 成功处理了第 {} 个客户端连接...", ++conn_count);
    }

    log::info("[Server] 资源清理完毕，进程即将退出。");
}

} // namespace

int main()
{
    async::run(demo_user_cancellation);
    async::run(demo_server_loop);
    return EXIT_SUCCESS;
}
