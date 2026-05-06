# 20. 重试策略模板（retry policy patterns）

> **源文件**：[examples/retry_policy_patterns/main.cpp](../../examples/retry_policy_patterns/main.cpp)

如果在多个 RPC 调用点都手写重试循环，代码很快会出现两类问题：

1. 每个地方退避策略不一致。
2. 可重试错误判断标准不一致。

这个示例把它拆成可复用模板：

1. 错误是否可重试（这里示例为 `timed_out`）。
2. 退避函数（fixed / exponential）。

## 示例怎么组织

`flaky_rpc` 会先失败几次再成功，确保每次运行都能看到完整重试路径。

策略调用是：

```cpp
co_await run_with_policy("fixed", [](int) { return 50ms; }, 2);
co_await run_with_policy("exponential", [](int attempt) {
    return std::chrono::milliseconds{ 25 * (1 << attempt) };
}, 3);
```

含义：

1. `fixed`：前 2 次失败，固定 50ms 退避，预期第 3 次成功。
2. `exponential`：前 3 次失败，退避 50/100/200ms，预期第 4 次成功。

## 落地建议

可以把这套模式用于外部依赖调用：

1. 先定义“哪些错误可重试”。
2. 再定义“每次重试前等待多久”。
3. 保留最大重试次数，避免无限重试。

## 运行

```bash
example.retry_policy_patterns
```

实际输出：

```
[2026-05-06 18:57:35.849] [107526] [info] [fixed] attempt 1 failed: generic:110
[2026-05-06 18:57:35.849] [107526] [info] [fixed] backoff 50ms
[2026-05-06 18:57:35.919] [107526] [info] [fixed] attempt 2 failed: generic:110
[2026-05-06 18:57:35.919] [107526] [info] [fixed] backoff 50ms
[2026-05-06 18:57:35.990] [107526] [info] [fixed] success on attempt 3
[2026-05-06 18:57:36.010] [107526] [info] [exponential] attempt 1 failed: generic:110
[2026-05-06 18:57:36.010] [107526] [info] [exponential] backoff 50ms
[2026-05-06 18:57:36.080] [107526] [info] [exponential] attempt 2 failed: generic:110
[2026-05-06 18:57:36.080] [107526] [info] [exponential] backoff 100ms
[2026-05-06 18:57:36.200] [107526] [info] [exponential] attempt 3 failed: generic:110
[2026-05-06 18:57:36.200] [107526] [info] [exponential] backoff 200ms
[2026-05-06 18:57:36.420] [107526] [info] [exponential] success on attempt 4
```

## 结语

把“错误分类”和“退避函数”抽出来后，重试行为可以在整个工程里保持一致，后续调整策略也只需要改一处。
