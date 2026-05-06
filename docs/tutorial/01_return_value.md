# 1.3 值传递与错误模型

> **前置阅读**：[1.2 挂起与恢复](01_sleep.md)
> **源文件**：[tutorial/03_return_value/main.cpp](../../tutorial/03_return_value/main.cpp)
> **下一节**：[1.4 并发调度与生命周期](01_co_spawn.md)

---

## 为什么不用异常

C++ 异常在同步代码里工作良好，但在异步协程环境中有一个根本性问题：**抛出点与捕获点不在同一个调用栈上**。

协程挂起后，调用者的栈帧已经不存在了。当协程在某个 io_uring 回调里被恢复时，它运行在事件循环的栈帧上，而不是原来调用者的栈帧上。传统的 `try/catch` 跨协程边界的行为是未定义的。

更重要的是，用类型系统来表达"这个操作可能失败"，比依赖异常有更好的工程属性：

- **调用者被迫处理失败**：返回 `std::expected<T,E>` 的函数，调用者必须检查结果，否则编译器或静态分析工具会发出警告
- **错误是值，可以传递、存储、组合**：后续章节的 `when_all` 能把多个 `expected` 打包成 `tuple<expected...>`，这在异常体系下很难实现
- **零开销**：`std::expected` 在成功路径上没有任何运行时开销

---

## 代码

```cpp
#include <blog.h>

namespace {

using namespace std::chrono_literals;

auto parse_int(std::string_view input)
    -> async::Task<std::expected<int, std::error_code>>
{
    co_await async::sleep_for(50ms);   // 模拟异步耗时
    co_return numeric_cast<int>(input);
}

auto run() -> async::Task<>
{
    // 成功路径
    auto r1 = co_await parse_int("42");
    if (r1)
        log::info("parsed: {}", *r1);

    // 失败路径
    auto r2 = co_await parse_int("abc");
    if (!r2)
        log::warning("error: {}", r2.error());
}

} // namespace

int main()
{
    async::run(run);
}
```

```
[2026-05-07 00:50:39.086] [208674] [info] parsed: 42
[2026-05-07 00:50:39.137] [208674] [warning] error: Invalid argument
```

---

## `Task<T>`：带返回值的协程

```cpp
auto parse_int(std::string_view input)
    -> async::Task<std::expected<int, std::error_code>>
```

`Task<T>` 中的 `T` 是协程产出的値类型。`co_await` 这个协程时，会得到一个 `T` 类型的値：

```cpp
auto r1 = co_await parse_int("42");
// r1 的类型是 std::expected<int, std::error_code>
```

---

## `std::expected<T, E>`

`std::expected<T, E>` 是 C++23 标准库的值类型，用来表示"成功得到 `T`，或失败得到错误 `E`"：

```cpp
// 成功：持有值，operator bool 返回 true
co_return value;                    // 隐式构造 expected 的成功状态

// 失败：持有错误，operator bool 返回 false
co_return std::unexpected{ "..." }; // 构造失败状态
```

使用时通过 `operator bool` 判断，成功路径用 `operator*` 取值，失败路径用 `.error()` 取错误：

```cpp
auto r = co_await parse_int("42");
if (r)
    use(*r);              // 成功，取值
else
    log::warning("{}", r.error()); // 失败，取错误
```

`log::info` / `log::warning` / `log::error` 均支持直接格式化错误值。`std::error_code` 通过 `format_as` 钩子格式化，输出等同于 `.message()` 的可读描述：

```cpp
log::warning("{}", r2.error());  // Invalid argument
```

---

## 类型如何驱动错误处理

`std::expected` 的关键工程属性是**错误无法被忽视**。与异常不同，它不会在调用栈上隐式传播——你必须显式检查每一个 `expected`，否则就是丢弃了错误信息。

这个约束在第 2 部分的网络代码中会更明显：每个 I/O 操作都返回 `expected`，错误处理路径在类型层面就是强制可见的。

---

## 本章小结

- `Task<T>` 中的 `T` 就是 `co_await` 这个协程得到的值类型
- `std::expected<T,E>` 用值语义表达可能失败的操作，取代异常
- 错误是普通的值，可以传递、存储、组合，在类型层面强制处理

> **下一节**：[1.4 并发调度与生命周期](01_co_spawn.md) — 同时运行多个协程，以及如何安全地管理它们的生命周期。
