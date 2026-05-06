# 17. 错误分类矩阵（error taxonomy）

> **源文件**：[examples/error_taxonomy/main.cpp](../../examples/error_taxonomy/main.cpp)

很多工程里最容易失控的不是 I/O 本身，而是错误处理分支：

1. 某些错误应该直接退出。
2. 某些错误应该重试。
3. 某些错误需要重连。

如果每个调用点都自己判断，代码会很快变得不可维护。这个示例展示一种更稳妥的做法：先分类，再映射动作。

## 可直接复用的模式

第一步，统一分类：

```cpp
auto classify(std::error_code ec) -> std::string_view
{
    if (ec == std::errc::operation_canceled) return "canceled";
    if (ec == std::errc::timed_out) return "timed_out";
    if (ec == std::errc::connection_reset) return "connection_reset";
    if (ec == std::errc::broken_pipe) return "broken_pipe";
    return "other";
}
```

第二步，统一动作策略：

1. `operation_canceled`：按正常收敛路径退出。
2. `timed_out`：进入重试或退避队列。
3. `connection_reset` / `broken_pipe`：关闭连接并重连。
4. 其他错误：记录并上报。

## 示例中的触发方式

1. 用预触发 stop token 触发 `operation_canceled`。
2. 用 `timeout(sleep_for(...), 50ms)` 触发 `timed_out`。
3. 构造一个 synthetic `connection_reset` 展示网络错误分支。

## 运行

```bash
example.error_taxonomy
```

实际输出：

```
[2026-05-06 18:57:35.432] [107522] [info] [stop_then] category=canceled -> exit gracefully
[2026-05-06 18:57:35.482] [107522] [info] [timeout] category=timed_out -> retry candidate
[2026-05-06 18:57:35.482] [107522] [info] [synthetic] category=connection_reset -> close and reconnect
```

## 下一步

接下来把最容易混淆的一对 API 放在同一页对照：见 [timeout_vs_stop_then](18_timeout_vs_stop_then.md)。
