# Waiting without blocking

> **源文件**：[examples/sleep/main.cpp](../../examples/sleep/main.cpp)

协程最重要的能力是**暂停自身，把线程还给事件循环**，等条件满足后再继续。`async::sleep_for` 是展示这一能力最直接的例子。

## 让协程等待 1 秒

```cpp
using namespace std::chrono_literals;

auto demo_sleep() -> async::Task<>
{
    log::info("before sleep");
    co_await async::sleep_for(1s);
    log::info("after sleep");
}
```

`co_await async::sleep_for(1s)` 这一行做了三件事：

1. 向 io_uring 提交一个 1 秒的定时器操作；
2. 挂起当前协程，**把线程交还给事件循环**；
3. 1 秒后定时器触发，事件循环恢复这个协程，继续执行下一行。

## 与 `std::this_thread::sleep_for` 的区别

`std::this_thread::sleep_for` 会让整个线程休眠，期间无法处理任何其他事件。`async::sleep_for` 只挂起**协程**，线程保持活跃，可以继续响应其他 I/O 或协程。

## 运行结果

```
[info] before sleep
# （等待约 1 秒）
[info] after sleep
```

## 下一步

现在我们知道如何暂停一个协程了。下一步是用协程完成真正的网络 I/O：[tcp_echo client](tcp_echo_client.md)。
