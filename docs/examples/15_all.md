# 15. 高层并行聚合（all）

> **源文件**：[examples/all/main.cpp](../../examples/all/main.cpp)

在“业务任务编排”视角（不是底层 awaiter 视角）下，`all` 往往比 `when_all` 更顺手。

可以把它理解成：

1. 同时启动一组 `Task<>`。
2. 等全部任务跑完再继续。
3. 不需要显式处理每个分支的底层 `expected` 聚合。

## 示例写法

```cpp
co_await async::all(
    all_worker("task-A", 120ms),
    all_worker("task-B", 400ms),
    all_worker("task-C", 260ms)
);
```

## 验证方式

观察日志顺序：

1. 三个 `started` 基本同时出现。
2. `completed` 按各自耗时先后出现。
3. 总耗时接近最长任务（这里约 400ms）。

这三条同时成立，就说明你不是串行执行。

## 与 when_all 的选择建议

1. 你需要“每个分支明确结果值”：选 `when_all`。
2. 你需要“任务编排简单可读”：选 `all`。

## 运行

```bash
example.all
```

实际输出：

```
[2026-05-06 18:46:07.154] [104656] [info] task-A: started
[2026-05-06 18:46:07.154] [104656] [info] task-B: started
[2026-05-06 18:46:07.154] [104656] [info] task-C: started
[2026-05-06 18:46:07.274] [104656] [info] task-A: completed
[2026-05-06 18:46:07.414] [104656] [info] task-C: completed
[2026-05-06 18:46:07.555] [104656] [info] task-B: completed
[2026-05-06 18:46:07.555] [104656] [info] all done (elapsed 400ms)
```

## 下一步

如果你要“谁先完成就保留谁，并尽快停止其他任务”，看：[any](16_any.md)。
