# 16. 并发竞速与协作取消（any）

> **源文件**：[examples/any/main.cpp](../../examples/any/main.cpp)

当业务目标是“先到先得”时，`any` 是直接的任务级方案。

典型场景：

1. 多条路径并发执行，谁先成功就采用谁。
2. 其余路径不需要继续浪费资源，应尽快收敛退出。

## 示例写法

```cpp
co_await async::any(
    async::task(simple_worker, "task-A", 120ms),
    async::task(simple_worker_front, "task-B", 400ms),
    async::task(simple_worker, "task-C", 260ms)
);
```

## 生效前提

`any` 只负责发出 stop 请求；落败任务能不能“很快停下来”，取决于任务内部是否有取消点。

本例中 `simple_worker` 每 20ms 调一次：

```cpp
co_await async::stop_then(async::sleep_for(step), token);
```

这就是协作式取消点，所以落败任务会很快退出。

## 验证方式

观察日志时，应该看到：

1. 三个任务先后 `started`。
2. 最快任务 `completed`。
3. 其他任务很快变成 `cancelled`。

## 与 when_any 的选择建议

1. `any`：Task 级别编排，关注业务任务收敛。
2. `when_any`：operation 级别组合，关注底层 awaiter 结果。

## 运行

```bash
example.any
```

实际输出：

```
[2026-05-06 18:46:07.556] [104657] [info] task-A: started
[2026-05-06 18:46:07.556] [104657] [info] task-B: started
[2026-05-06 18:46:07.556] [104657] [info] task-C: started
[2026-05-06 18:46:07.677] [104657] [info] task-A: completed
[2026-05-06 18:46:07.677] [104657] [info] task-B: cancelled
[2026-05-06 18:46:07.677] [104657] [info] task-C: cancelled
[2026-05-07 03:32:05.372] [258061] [info] any done (elapsed 120ms)
```

## 下一步

接下来进入错误处理视角：如何把错误码映射成统一动作策略，见：[error taxonomy](17_error_taxonomy.md)。
