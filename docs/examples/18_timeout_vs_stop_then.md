# 18. timeout vs stop_then（行为对照）

> **源文件**：[examples/timeout_vs_stop_then/main.cpp](../../examples/timeout_vs_stop_then/main.cpp)

这两个 API 最容易被误用，因为它们都能“提前结束等待”，但语义完全不同。

## 选择建议

1. 想响应外部取消信号（用户中断、上游停机）：选 `stop_then`。
2. 想给单次操作设置最大等待时长：选 `timeout`。

## 示例设计

示例把同一个 `sleep_for(500ms)` 放进两种机制：

1. `stop_then`：120ms 由外部线程触发 stop。
2. `timeout`：固定 200ms 超时。

代码核心：

```cpp
auto canceled = co_await async::stop_then(async::sleep_for(500ms), stop.get_token());
auto timed = co_await async::timeout(async::sleep_for(500ms), 200ms);
```

## 验证方式

1. `stop_then` 的 elapsed 是否贴近 stop 触发时间。
2. `timeout` 的 elapsed 是否贴近 timeout 设定值。

如果这两点成立，就说明两种机制行为符合预期。

## 运行

```bash
example.timeout_vs_stop_then
```

实际输出：

```
[2026-05-06 18:57:35.605] [107523] [info] stop_then result=generic:125 elapsed=120ms
[2026-05-06 18:57:35.805] [107523] [info] timeout result=generic:110 elapsed=200ms
```

## 下一步

继续看完整服务骨架：如何把 stop 信号接入 accept 循环并实现优雅停机，见 [graceful_shutdown_server](19_graceful_shutdown_server.md)。
