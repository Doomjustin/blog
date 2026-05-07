# 6.1 信号与事件循环集成：优雅停机与系统生命周期

> **前置知识**：本章假设你已读完 [3.1（stop_then 外部取消）](05_stop_then.md)，理解协作式取消的设计哲学。
> **示例源码**：[examples/graceful_shutdown_server/main.cpp](../../examples/graceful_shutdown_server/main.cpp)
> **下一节**：[6.2 线程模型与局部性](16_threading.md)

---

## 当前示例解决的问题

本章关注“如何把停机请求变成可推演的协程控制流”。

当前可运行实现不在 tutorial 目录，而在
[examples/graceful_shutdown_server/main.cpp](../../examples/graceful_shutdown_server/main.cpp)。

它做了三件事：

1. 用 `stop_source` 保存外部停止意图。
2. 用 `stop_then(acceptor.async_accept(), stop_token)` 打断 accept 挂起点。
3. 让已 `co_spawn` 的会话继续自然收敛，避免粗暴中断。

---

## 关键代码

停止 `accept`：

```cpp
auto client = co_await async::stop_then(acceptor.async_accept(), stop);
if (!client) {
    if (client.error() == std::errc::operation_canceled) {
        log::info("[server] accept canceled, shutdown complete");
        co_return;
    }
    continue;
}
```

触发停止：

```cpp
std::stop_source stop;
async::co_spawn(server(port, stop.get_token()));
co_await probe_client(port);
stop.request_stop();
```

---

## 运行方式

```bash
./build/examples/graceful_shutdown_server/example.graceful_shutdown_server
```

如果你本机未看到日志，请先检查日志级别；本章重点是控制流形状，而不是日志格式。

---

## 架构边界

- `request_stop()` 只发协作取消信号，不会强制杀死正在执行的会话。
- 若业务需要“限时停机”，应在此骨架上叠加 deadline（例如 stop 后再加 timeout 窗口）。

---

## 本章小结

6.1 的可复用模板是：`stop_source` 发信号，`stop_then` 打断阻塞点，会话协程自行收敛。这样停机流程就能被测试、被推演，而不是依赖进程级强杀。

> **下一节**：[6.2 线程模型与局部性](16_threading.md)
