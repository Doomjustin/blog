# Writing your first coroutine

> **源文件**：[examples/hello_coroutine/main.cpp](../../examples/hello_coroutine/main.cpp)

让我们从最简单的一个程序开始。

## 编写一个协程函数

本库用 `async::Task<>` 作为协程的返回类型。你只需要把普通函数的 `void` 换成它，函数体里就可以用 `co_await` 和 `co_return`：

```cpp
auto hello() -> async::Task<>
{
    log::info("hello from coroutine");
    co_return;
}
```

## 运行它

协程函数不像普通函数那样直接调用——它需要一个事件循环来驱动。`async::run` 做的正是这件事：启动 io_uring 事件循环，把传入的协程函数作为入口提交，然后阻塞直到它完成：

```cpp
int main()
{
    async::run(hello);
}
```

这就是使用本库的最小模式。你不需要手动创建 `IOContext`，也不需要管理线程——`async::run` 替你完成了这一切。

## 运行结果

```
[info] hello from coroutine
```

## 下一步

协程能做的不仅是打印一行日志。下一个示例展示如何让协程**等待**而不阻塞线程：[sleep](02_sleep.md)。
