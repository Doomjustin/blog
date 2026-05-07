# 附录：CPU 密集型任务的工作线程适配

> **前置知识**：[第 6.1 章（线程模型）](15_threading.md)
> **源文件**：[tutorial/22_custom_awaiter/main.cpp](../../tutorial/22_custom_awaiter/main.cpp)
> **可执行文件**：`./build/tutorial/22_custom_awaiter/tutorial.22_custom_awaiter`

---

## 问题场景

`IOContext` 是单线程事件循环。如果在协程里直接调用阻塞函数（如 bcrypt 密码校验、JSON 反序列化），那 200ms 内所有其他连接的网络事件都会堆积在 io_uring CQ 里，导致服务器对外表现为"卡顿"。

```cpp
auto handle_login(std::string username) -> async::Task<>
{
    // 💀 这会阻塞整个 IO 线程
    bool ok = bcrypt_verify(username, stored_hash);
}
```

**解决方案**：把 CPU 任务扔给工作线程，完成后把结果送回 IO 线程——整个过程用 `co_await` 表达。

---

## 当前示例对应的业务流程

```cpp
auto handle_login(std::string username, std::string password) -> async::Task<>
{
    log::info("[IO 线程 {}] 收到登录请求: user={}", std::this_thread::get_id(), username);

    // bcrypt 在工作线程执行，IO 线程继续服务其他请求
    auto result = co_await run_on_thread([pwd = password, hash = stored_hash]() {
        return bcrypt_verify(pwd, hash);
    });

    // 自动回到同一个 IO 线程
    if (!result)
        log::info("[IO 线程 {}] 操作取消: {}", std::this_thread::get_id(), result.error().message());
    else
        log::info("[IO 线程 {}] 验证结果: {}", std::this_thread::get_id(), *result ? "成功" : "失败");
}
```

关键特性：

1. **非阻塞**：IO 线程在 `co_await` 时立刻挂起，空出来处理其他连接。
2. **线程安全**：结果通过 `async::post` 原子投递回 IO 线程，无需业务端手工同步。
3. **可取消**：与 `timeout/when_any` 组合时，取消信号能正确传播，不产生 UAF。
4. **返回 `std::expected`**：取消时返回 `std::unexpected(operation_canceled)`，业务端显式检查。

运行命令：

```bash
./build/tutorial/22_custom_awaiter/tutorial.22_custom_awaiter
```

实测输出：

```text
[info] === Demo: 将 CPU 密集型任务卸载到工作线程 ===
[info] [IO 线程 140489611001664] 收到登录请求: user=alice
[info] [工作线程 140489603608896] 正在进行 bcrypt 校验...
[info] [IO 线程 140489611001664] 验证结果: 成功
```

---

## 核心设计

框架在 `async::run_on_thread()` 底层使用了 `from_callback<R>()` 通用桥接器，其核心做法：

1. 用 `std::shared_ptr<State>` 将结果和协程句柄移出协程帧——即使协程被取消销毁，工作线程仍可安全回调。
2. 用原子位 `claimed_` 进行 CAS 竞态保护——取消与回调竞争时，只有胜者有权修改状态。
3. 通过 `async::post(IOContext, lambda)` 切回 IO 线程——确保 `await_resume()` 在正确的线程执行。
4. 若取消了，`await_resume()` 返回 `std::unexpected`，业务层显式处理。

---

## 架构边界

- `run_on_thread` 针对的是 **CPU 密集型** 任务（bcrypt、JSON 解析、图片处理等）。
- 如果目标是 **网络 IO**（HTTP 请求、数据库查询），应该直接用异步 API（如 libcurl 异步模式、pq 异步模式），而非工作线程。
- 工作线程数应结合 CPU 核数和实际负载做压测调优，不应盲目增加。
