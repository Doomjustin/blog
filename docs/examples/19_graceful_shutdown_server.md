# 19. 优雅停机服务骨架（graceful shutdown）

> **源文件**：[examples/graceful_shutdown_server/main.cpp](../../examples/graceful_shutdown_server/main.cpp)

服务端开发通常都要解决三个问题：

1. 如何停机时不丢请求。
2. 如何让 `accept` 循环干净退出。
3. 如何保证进程最终可预测地结束。

这个示例给出一个最小、可复用的骨架。

## 推荐结构

```text
run()
├── co_spawn(server(port, token))
├── co_await probe_client(port)
└── request_stop() -> accept 取消 -> server co_return
```

关键思路：

1. server 的 `accept` 不要裸等，必须包 `stop_then`。
2. stop 触发后，把 `operation_canceled` 视为正常退出路径。
3. 在 demo 里先做一次探活请求，确认服务可用，再触发停机。

## 关键代码

```cpp
auto client = co_await async::stop_then(acceptor.async_accept(), stop);
if (!client && client.error() == std::errc::operation_canceled)
    co_return;
```

## 验证方式

日志里应满足以下顺序：

1. 先看到 server 监听地址。
2. probe client 成功收到 echo。
3. demo 打印 finished。
4. accept 被取消并打印 shutdown complete。

## 运行

```bash
example.graceful_shutdown_server
```

实际输出：

```
[2026-05-06 18:57:35.807] [107525] [info] [server] listening on 127.0.0.1:39699
[2026-05-06 18:57:35.807] [107525] [info] [client] got echo: ping
[2026-05-06 18:57:35.827] [107525] [info] [demo] graceful shutdown finished
[2026-05-06 18:57:35.827] [107525] [info] [server] accept canceled, shutdown complete
```

## 下一步

最后一篇看重试策略模板：如何把固定退避和指数退避统一成可复用模式，见 [retry_policy_patterns](20_retry_policy_patterns.md)。
