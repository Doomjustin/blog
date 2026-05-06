# 4.3 任务树的协作取消：`any`

> **前置知识**：本章假设你已读完 [4.1（when_all）](08_when_all.md) 与 [4.2（when_any）](09_when_any.md)。

---

## 为什么还需要 `any`

`when_any` 解决的是 **I/O operation 级** 竞争：谁先完成就返回谁，其他 operation 自动取消。

`any` 解决的是 **Task 级** 竞争：并发运行多个协程任务，第一个任务完成后向同级任务广播停止信号（`stop_token`），让整棵任务树尽快收敛。

```cpp
co_await async::any(
    async::task(cancellable_worker, "task-A", 120ms),
    async::task(cancellable_worker, "task-B", 400ms),
    async::task(cancellable_worker, "task-C", 260ms)
);
```

---

## `any` 与 `when_any` 的边界

| 维度 | `when_any` | `any` |
|------|------------|-------|
| 编排层级 | operation 级 | Task 级 |
| 参数类型 | `cancelable_operation` | `stop_awaitable_provider`（通常由 `async::task` 生成） |
| 返回值 | `expected<R,E>`（胜者结果） | `void`（任务收敛完成） |
| 取消机制 | 内部取消其余 operation | `scope.request_stop()` + `stop_token` 协作取消 |

---

## 完整示例（来自 tutorial.14_all_race）

```cpp
#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto worker(const char* name, std::chrono::milliseconds duration) -> async::Task<>
{
    log::info("{}: started", name);
    co_await async::sleep_for(duration);
    log::info("{}: completed", name);
}

auto demo_all() -> async::Task<>
{
    log::info("=== demo 1: async::all ===");
    auto start = std::chrono::steady_clock::now();

    co_await async::all(
        worker("A", 120ms),
        worker("B", 400ms),
        worker("C", 260ms)
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    log::info("all done in {}ms (expected ~400ms)", elapsed.count());
}

auto cancellable_worker(
    const char* name,
    std::chrono::milliseconds total,
    std::stop_token token
) -> async::Task<>
{
    log::info("{}: started", name);

    auto remaining = total;
    constexpr auto quantum = 20ms;

    while (remaining > 0ms) {
        auto step = std::min(remaining, quantum);
        auto result = co_await async::stop_then(async::sleep_for(step), token);
        if (!result) {
            log::info("{}: cancelled", name);
            co_return;
        }
        remaining -= step;
    }

    log::info("{}: completed", name);
}

auto demo_any() -> async::Task<>
{
    log::info("\n=== demo 2: async::any ===");
    auto start = std::chrono::steady_clock::now();

    co_await async::any(
        async::task(cancellable_worker, "task-A", 120ms),
        async::task(cancellable_worker, "task-B", 400ms),
        async::task(cancellable_worker, "task-C", 260ms)
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    log::info("any done in {}ms (expected ~120ms)", elapsed.count());
}

} // namespace

int main()
{
    async::run(demo_all);
    async::run(demo_any);
    return EXIT_SUCCESS;
}
```

---

## 运行方式

```bash
./build/tutorial/14_all_race/tutorial.14_all_race
```

实际输出（节选）：

```
[2026-05-07 03:32:01.773] [258030] [info] === demo 2: async::any ===
[2026-05-07 03:32:01.773] [258030] [info] task-A: started
[2026-05-07 03:32:01.773] [258030] [info] task-B: started
[2026-05-07 03:32:01.773] [258030] [info] task-C: started
[2026-05-07 03:32:01.894] [258030] [info] task-A: completed
[2026-05-07 03:32:01.894] [258030] [info] task-B: cancelled
[2026-05-07 03:32:01.894] [258030] [info] task-C: cancelled
[2026-05-07 03:32:01.894] [258030] [info] any done in 120ms (expected ~120ms)
```

---

## 关键注意点

1. 传给 `async::task` 的任务函数，`std::stop_token` 必须按值接收。
2. 任务内部需要有取消点（如 `stop_then(...)`），否则即使收到 stop 信号也不会尽快退出。
3. `any` 用于 Task 编排；如果你只是在若干 I/O awaiter 中选最先完成者，优先用 `when_any`。

---

## 本章小结

`any` 是 Task 级“先到先得”组合器：第一个完成者触发停止信号，其他任务通过协作取消快速收敛。它与 `when_any` 互补，分别覆盖任务编排层与 operation 组合层。

下一节：4.4 Channel（跨任务通信）。
