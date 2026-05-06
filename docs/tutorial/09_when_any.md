# 4.2 竞速与抢占：`when_any`

> **前置知识**：本章假设你已读完 [4.1（when_all）](08_when_all.md)。

---

## 换个问题：不需要所有人完成

上一节的 `when_all` 等待"所有操作都完成"。有时我们只需要**最快的那个**——把同一个请求发给多个 endpoint，取先响应的，其余的丢弃。

`when_any` 正好对应这个场景：

```cpp
auto result = co_await async::when_any(op0, op1, op2);
```

- 所有操作**同时**提交给 io_uring
- 第一个返回 CQE 的操作"胜出"
- 立即取消其余未完成的操作
- 返回胜者的结果（`std::expected<R, error_code>`，不是 tuple）

---

## 完整代码

```cpp
#include <chrono>
#include <cstdlib>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

/// 演示 1：两个 endpoint 竞速，取先返回的
///
/// when_any 并发提交所有操作，第一个完成的"胜出"：
///   - 返回胜者的结果（std::expected<void, error_code>）
///   - 自动取消其余未完成的操作
auto demo_any_endpoints() -> async::Task<>
{
    log::info("=== demo 1: compare two endpoints ===");
    auto start = std::chrono::steady_clock::now();

    // 模拟两个 endpoint：A 慢（300ms），B 快（100ms）
    // when_any 取先完成的，另一个被自动取消
    auto result = co_await async::when_any(
        async::sleep_for(300ms),  // endpoint A
        async::sleep_for(100ms)   // endpoint B（winner）
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (result)
        log::info("winner responded in {}ms (expected ~100ms)", elapsed.count());
    else
        log::error("both failed: {}", result.error());
}

/// 演示 2：三方竞速，取最快的
///
/// 返回值是胜者的 expected<void, error_code>，不是 tuple。
/// 所有参数的 resume_type 必须相同（此处均为 void）。
auto demo_any_three() -> async::Task<>
{
    log::info("\n=== demo 2: compare three endpoints ===");
    auto start = std::chrono::steady_clock::now();

    // 三个"请求"，速度不同
    auto result = co_await async::when_any(
        async::sleep_for(500ms),   // 慢
        async::sleep_for(100ms),   // 最快（winner）
        async::sleep_for(300ms)    // 中等
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (result)
        log::info("fastest responded in {}ms (expected ~100ms)", elapsed.count());
    else
        log::error("error: {}", result.error());
}

/// 演示 3：胜者失败时的行为
///
/// 如果最先返回的操作失败，when_any 照样返回该失败结果，
/// 并取消其余操作。调用者通过检查 expected 来判断是否需要回退。
auto demo_winner_fails() -> async::Task<>
{
    log::info("\n=== demo 3: first to complete fails ===");
    auto start = std::chrono::steady_clock::now();

    // endpoint A：200ms 后超时失败
    // endpoint B：500ms 后正常完成
    // A 先完成（以失败告终），when_any 返回 A 的失败结果并取消 B
    auto result = co_await async::when_any(
        async::timeout(async::sleep_for(5s), 200ms),  // A: 先完成，但超时失败
        async::sleep_for(500ms)                        // B: 慢，被取消
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (!result)
        log::info("first to finish failed with {} at {}ms (expected ~200ms, timed_out)",
            result.error(), elapsed.count());
    else
        log::info("unexpected success");
}

} // namespace

int main()
{
    async::run(demo_any_endpoints);
    async::run(demo_any_three);
    async::run(demo_winner_fails);
    return EXIT_SUCCESS;
}
```

---

## 运行方式

```bash
./build/tutorial/13_when_any/tutorial.13_when_any
```

```
[info] === demo 1: compare two endpoints ===
[info] winner responded in 100ms (expected ~100ms)

[info] === demo 2: compare three endpoints ===
[info] fastest responded in 100ms (expected ~100ms)

[info] === demo 3: first to complete fails ===
[info] first to finish failed with Connection timed out at 200ms (expected ~200ms, timed_out)
```

---

## 语义分析

### `when_any` vs `when_all` 的核心区别

| 维度 | `when_all` | `when_any` |
|------|-----------|-----------|
| **等待策略** | 所有操作完成 | 第一个完成 |
| **返回类型** | `tuple<expected<R0,E>, expected<R1,E>, ...>` | `expected<R, E>`（胜者的结果） |
| **未完成操作** | 不存在（都等完了） | 自动取消 |
| **总耗时** | 最慢的那个 | 最快的那个 |
| **典型场景** | 批量请求全部完成后合并 | 多副本冗余查询，取首个响应 |

### 时间线对比

```
when_all（3个操作）：
t=0ms    A(300), B(100), C(200)  开始
t=100ms  B完成
t=200ms  C完成
t=300ms  A完成 ← 此时返回（耗时 300ms）

when_any（3个操作）：
t=0ms    A(500), B(100), C(300)  开始
t=100ms  B完成 ← 此时返回（耗时 100ms），A和C被取消
```

### 返回值不是 tuple

`when_any` 返回的是胜者的 `expected<R, error_code>`，而不是 tuple。这意味着：

- 所有参数的结果类型 `R` 必须相同（否则编译报错）
- 返回值直接用，无需解构：

```cpp
// when_all：解构 tuple
auto [r0, r1] = co_await async::when_all(op0, op1);
if (r0 && r1) { ... }

// when_any：直接使用
auto result = co_await async::when_any(op0, op1);
if (result) { ... }
```

### 胜者失败不等于整体失败

demo 3 展示了一个重要语义：**胜者可以是"以失败告终的最快者"**。

```
A（timeout 200ms）先完成 → 以 timed_out 失败
B（500ms）更慢 → 被取消，永远没有机会完成
```

`when_any` 不区分"成功完成"和"失败完成"——谁先有结果谁就胜出，无论结果是成功还是失败。如果需要"取第一个成功的"，需要在外层加重试逻辑。

---

## `when_any` 与 `any` 的区别

`when_any` 和 `any` 都是"取最快的"，但针对不同层次的操作：

| | `when_any` | `any` |
|--|-----------|-------|
| **操作类型** | `cancelable_operation`（I/O 操作） | `Task<>`（协程任务） |
| **取消机制** | 通过 io_uring 链接 SQE 取消 | 通过 `stop_token` 协作取消 |
| **适用场景** | `sleep_for`、`recv`、`send` 等 | 任意协程逻辑 |

`any` 在 4.3 节详细介绍。

---

## 本章小结

- **`when_any` 取最快的**：所有操作并发提交，第一个返回的胜出，其余自动取消。
- **返回胜者的 `expected<R,E>`**：不是 tuple，结果类型 `R` 必须对所有参数一致。
- **胜者失败也算胜**：无论成功还是失败，第一个有结果的就返回。
- **总耗时 = 最快的那个**：与 `when_all` 的"最慢的那个"相对。

下一节：[4.3 任务树的协作取消（any）](10_any.md) — `any` 组合器，子任务失败时如何通过信号链安全销毁同级协程帧。
