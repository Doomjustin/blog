# 14. 并行竞争首个完成者（when_any）

> **源文件**：[examples/when_any/main.cpp](../../examples/when_any/main.cpp)

当场景只关心“最先完成的结果”时，`when_any` 比 `when_all` 更合适。

典型场景：

1. 多个等价数据源，谁先返回就用谁。
2. 快路径和慢路径并发，优先使用快路径。
3. 竞速探测（例如多个 endpoint 试连）。

## 示例写法

```cpp
auto result = co_await async::when_any(
    async::sleep_for(500ms),
    async::sleep_for(100ms),
    async::sleep_for(300ms)
);
```

这段代码的语义是：

1. 三个分支并发提交。
2. 第一个完成时，外层协程立刻恢复。
3. 其余分支会被取消，避免继续占用资源。

## 返回值怎么读

本例三个分支 `resume_type` 相同（都是 `void`），所以返回值是 `std::expected<void, std::error_code>`。

如果换成异构分支（不同返回类型），返回会变成 variant 形式，按 index 区分胜出者。

## 与 any 的选择建议

1. `when_any`：更偏 operation 组合层，直接返回 operation 结果。
2. `any`：更偏 Task 编排层，依赖任务内部取消点。

## 运行

```bash
example.when_any
```

实际输出：

```
[2026-05-06 18:46:07.152] [104655] [info] first timer fired (elapsed 100ms, expected ~100ms)
```

## 下一步

如果要在 `Task<>` 层做“并发后等待全部结束”，看：[all](15_all.md)。
