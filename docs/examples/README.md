# Examples 导读

这份目录用于快速定位 `examples/` 下每个示例的目标、适用场景和推荐阅读顺序。

## 推荐阅读顺序

1. [01_hello_coroutine](01_hello_coroutine.md)
2. [02_sleep](02_sleep.md)
3. [05_concurrent_tasks](05_concurrent_tasks.md)
4. [03_tcp_echo_client](03_tcp_echo_client.md)
5. [04_tcp_echo_server](04_tcp_echo_server.md)
6. [06_timeout_echo_server](06_timeout_echo_server.md)
7. [07_line_protocol](07_line_protocol.md)
8. [08_scatter_gather](08_scatter_gather.md)
9. [09_zero_copy_send](09_zero_copy_send.md)
10. [10_stop_then_sleep](10_stop_then_sleep.md)
11. [11_stop_then_request](11_stop_then_request.md)
12. [12_stop_then_resilient](12_stop_then_resilient.md)
13. [13_when_all](13_when_all.md)
14. [14_when_any](14_when_any.md)
15. [15_all](15_all.md)
16. [16_race](16_race.md)
17. [17_error_taxonomy](17_error_taxonomy.md)
18. [18_timeout_vs_stop_then](18_timeout_vs_stop_then.md)
19. [19_graceful_shutdown_server](19_graceful_shutdown_server.md)
20. [20_retry_policy_patterns](20_retry_policy_patterns.md)

## 速查索引（按主题）

### 基础 coroutine

- [01_hello_coroutine](01_hello_coroutine.md): 最小可运行 `Task<>` 与 `async::run`。
- [02_sleep](02_sleep.md): 使用 `sleep_for` 做非阻塞等待。
- [05_concurrent_tasks](05_concurrent_tasks.md): 使用 `co_spawn` 并发运行多个任务。

### TCP 与协议处理

- [03_tcp_echo_client](03_tcp_echo_client.md): TCP client 发送/接收基本流程。
- [04_tcp_echo_server](04_tcp_echo_server.md): TCP server accept + session 基本结构。
- [06_timeout_echo_server](06_timeout_echo_server.md): 在 echo server 中引入 timeout 控制。
- [07_line_protocol](07_line_protocol.md): 面向行协议的解析与处理。

### I/O 性能路径

- [08_scatter_gather](08_scatter_gather.md): Scatter/Gather I/O 用法。
- [09_zero_copy_send](09_zero_copy_send.md): `zero_copy_send` 示例与适用场景。

### 取消与韧性

- [10_stop_then_sleep](10_stop_then_sleep.md): `stop_then` 的最小取消示例。
- [11_stop_then_request](11_stop_then_request.md): 请求循环中的逐操作取消。
- [12_stop_then_resilient](12_stop_then_resilient.md): 组合 `stop_then` + `timeout` + retry policy。

### 并行组合器

- [13_when_all](13_when_all.md): 并发等待全部完成，返回 tuple。
- [14_when_any](14_when_any.md): 并发竞争首个完成，取消其余分支。
- [15_all](15_all.md): `Task<>` 级别的高层并行聚合封装。
- [16_race](16_race.md): 首个完成后请求停止其余任务（协作式取消）。

### 错误处理与生产化

- [17_error_taxonomy](17_error_taxonomy.md): 常见错误码分类与动作矩阵。
- [18_timeout_vs_stop_then](18_timeout_vs_stop_then.md): 对照 `timeout` 与 `stop_then` 触发语义。
- [19_graceful_shutdown_server](19_graceful_shutdown_server.md): 最小优雅停机 server 骨架。
- [20_retry_policy_patterns](20_retry_policy_patterns.md): 固定/指数退避的重试策略模板。

## 选型建议

- 需要“底层 operation 结果聚合（tuple<expected...>）”：优先看 `when_all`。
- 需要“Task 级并发并等待全部结束”：优先看 `all`。
- 需要“底层 operation 竞争首个完成”：优先看 `when_any`。
- 需要“Task 级首个完成并协作式取消其余任务”：优先看 `race`。
- 需要可取消 I/O：从 `stop_then` 系列（10/11/12）开始。
- 需要高吞吐 I/O：先看 `scatter_gather` 与 `zero_copy_send`。
