# 13. 并行等待全部完成（when_all）

> **源文件**：[examples/when_all/main.cpp](../../examples/when_all/main.cpp)

常见需求包括：

1. 同时发起多个异步步骤。
2. 必须等全部完成后再继续。
3. 还想保留每个步骤的独立成功/失败结果。

`async::when_all` 就是为这个场景准备的。

## 示例写法

```cpp
auto [r0, r1, r2] = co_await async::when_all(
    async::sleep_for(300ms),
    async::sleep_for(100ms),
    async::sleep_for(200ms)
);
```

这段代码的语义是：

1. 三个 awaiter 并行提交，不是串行等待。
2. 返回值是 `tuple<expected...>`，每个槽位对应一个分支。
3. 外层协程只会在“全部完成”后恢复。

## 验证方式

示例里三个 sleep 时长是 300ms、100ms、200ms。并行执行时：

1. 总耗时应接近 300ms（最长分支）。
2. 不应接近 600ms（串行总和）。

如果把 sleep 换成真实 I/O，也可以用同样方法判断：总耗时应由最长分支主导，而不是总和。

## 与 all 的选择建议

两者都能表达“等全部完成”，但关注点不同：

1. `when_all`：更偏 operation 组合，强调每个分支结果可见。
2. `all`：更偏 Task 编排，强调把一组任务跑完。

## 运行

```bash
example.when_all
```

实际输出：

```
[2026-05-06 18:46:07.050] [104654] [info] all timers done (elapsed 300ms, expected ~300ms)
```

## 下一步

如果需求从“全部完成再继续”切换成“谁先完成先用谁”，继续看：[when_any](14_when_any.md)。
