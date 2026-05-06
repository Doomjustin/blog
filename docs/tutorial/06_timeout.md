# 3.2 局部时间约束：timeout 与 deadline 语义

> **前置知识**：本章假设你已读完 [3.1（外部取消）](05_stop_then.md)，理解 `stop_token` 和协作式取消。

---

## timeout 与 stop_then 的区别

两者都能提前中断操作，但语义不同：

| 方面 | `stop_then` | `timeout` |
|------|---|---|
| **触发条件** | 外部调用 `stop_source.request_stop()` | 自动计时，时间到达后 |
| **时间来源** | 调用者决定 | 相对时间（duration） |
| **典型应用** | 取消键入、用户操作 | 端口超时、操作截止 |
| **错误码** | `operation_canceled` | `timed_out` |

`timeout` 会自动开启一个内核定时器（io_uring linked timeout），时间到达时自动触发中断。不需要外部线程或信号源。

---

## 完整代码

```cpp
#include <chrono>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto demo() -> async::Task<>
{
    log::info("timeout scenario: 5-second sleep with 1-second timeout");

    auto result = co_await async::timeout(
        async::sleep_for(5s),
        1s
    );

    if (!result) {
        if (result.error() == std::errc::timed_out)
            log::info("timed out at 1s (as expected)");
        else
            log::error("error: {}", result.error());
    }
    else {
        log::info("sleep completed (unexpected)");
    }
}

} // namespace

int main()
{
    async::run(demo);
    return EXIT_SUCCESS;
}
```

---

## 运行方式

```bash
./build/tutorial/10_timeout/tutorial.10_timeout
```

```
[info] timeout scenario: 5-second sleep with 1-second timeout
[info] timed out at 1s (as expected)
```

---

## 逐步解析

### `co_await async::timeout(operation, duration)`

```cpp
auto result = co_await async::timeout(
    async::sleep_for(5s),
    1s
);
```

`timeout` 将任意单次异步操作包装成有时间限制的版本。返回 `std::expected<T, std::error_code>`，其中 `T` 是原操作的返回类型。

包装器内部向 io_uring 提交两个 SQE：
1. **主操作** SQE：`sleep_for(5s)`
2. **链接超时** SQE：通过 `io_uring_prep_link_timeout` 与主操作链接，时间到达后自动中断

内核会等待两个 CQE 都到达，然后返回先发生者的结果。

### 超时时返回 `timed_out`

```cpp
if (!result) {
    if (result.error() == std::errc::timed_out)
        log::info("timed out at 1s");
}
```

`timeout` 的错误码与 `stop_then` 不同。`stop_then` 返回 `operation_canceled`，而 `timeout` 返回 `timed_out`。这允许调用者区分"主动取消"和"时间到期"两种中断原因。

### 链接超时 vs 独立超时

io_uring 的 linked timeout 有两种用法：

1. **链接到主操作**（本例）：内核等两个 CQE 都到达；如果主操作先完成，链接超时 SQE 自动被取消，不会返回超时。
2. **独立超时**（不讲）：超时 SQE 独立工作，无论主操作是否完成都会投递超时 CQE。

教程使用的是链接模式，语义更清晰——**我给你 1s 去完成这个操作，超过就中断**。

### 应用示例：接收超时

```cpp
auto result = co_await async::timeout(
    socket.async_receive_some(buf),
    30s
);

if (!result) {
    if (result.error() == std::errc::timed_out)
        log::warning("no data within 30 seconds");
    else
        log::error("recv failed: {}", result.error());
    co_return;
}
```

典型用途：idle timeout（连接长时间无数据则断开）、响应期限（等待对端回复的截止时间）。

---

## 本章小结

`timeout` 通过 io_uring linked timeout 机制为任意操作加入时间限制。与 `stop_then` 的主动取消不同，`timeout` 是被动的自动中断，错误码为 `timed_out`。两者一起，形成对"何时停止"的完整描述：`stop_then` 说"用户要求停止"，`timeout` 说"时间已到"。

下一节：[3.3 错误分类与组合](07_error_handling.md) — 对比两种取消机制，理解 `timed_out` vs `operation_canceled` 的区别，以及如何根据错误类型决定重试策略。
