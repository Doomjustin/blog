#include <blog.h>

namespace {

using namespace std::chrono_literals;

// 模拟一项耗时的异步工作，接受值语义参数
auto process(std::string message, int id) -> async::Task<>
{
    co_await async::sleep_for(100ms);
    log::info("task {}: {}", id, message);
}

auto run() -> async::Task<>
{
    // 启动三个并发任务；每条消息以移动语义传入，run() 不持有任何引用
    for (int i = 0; i < 3; ++i) {
        std::string msg = std::format("message-{}", i);
        async::co_spawn(process(std::move(msg), i));
    }
    log::info("all tasks spawned, run() returning");
    // run() 在此返回；事件循环继续驱动三个已派生的任务直至全部完成
    co_return;
}

} // namespace

int main()
{
    async::run(run);
}
