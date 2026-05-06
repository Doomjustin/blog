# 09. 零拷贝发送（Zero-Copy Send）

## 先看运行效果

```bash
# Terminal 1: Start zero-copy server
$ example.zero_copy_send 8081
[INFO] Zero-copy server listening on port 8081

# Terminal 2: Connect to the server
$ nc localhost 8081
Zero-copy message from io_uring SEND_ZC
Zero-copy message from io_uring SEND_ZC
Zero-copy message from io_uring SEND_ZC
Connection closed by foreign host.

# Terminal 1 output:
[INFO] Client connected: 127.0.0.1:49153
[INFO] Sent 41 bytes (zero-copy) to 127.0.0.1:49153
[INFO] Sent 41 bytes (zero-copy) to 127.0.0.1:49153
[INFO] Sent 41 bytes (zero-copy) to 127.0.0.1:49153
[INFO] Client disconnected: 127.0.0.1:49153
```

---

## 核心理念

传统的网络发送流程涉及**两次内存复制**：

```
用户空间缓冲区 
  ↓ (copy_to_kernel)
内核 socket 缓冲区 
  ↓ (DMA copy)
网卡内存
```

`io_uring` 的 `IORING_OP_SEND_ZC` 操作绕过了第一步，让网卡驱动直接从用户空间读取数据：

```
用户空间缓冲区 
  ↓ (直接 DMA，无内核复制)
网卡内存
```

**代价**：缓冲区在 `co_await` 期间（协程挂起时）不能被释放或修改，直到内核的 notif CQE 到达。

---

## API 设计

### 标记类型 `ZeroCopyT`

为了在编译时强制开发者认可"缓冲区必须保活"这一约束，库使用了一个 **标记类型**：

```cpp
struct ZeroCopyT {
    std::span<const std::byte> span;
};

auto zero_copy(std::span<const std::byte> s) noexcept -> ZeroCopyT
{
    return { s };
}
```

调用方必须显式包裹缓冲区：`net::zero_copy(buffer)` 而不是 `buffer`。

### 返回值与 CQE

`IORING_OP_SEND_ZC` 返回**两个完成事件**（CQE）：

| CQE | `res` 字段 | `flags` 字段 | 含义 |
|-----|-----------|-------------|------|
| 1 | 发送字节数 | `IORING_CQE_F_MORE` | 发送完成，后续还有 notif CQE |
| 2 | 0 | `IORING_CQE_F_NOTIF` | 内核已释放对缓冲区的引用 |

库内部处理这两个 CQE，**协程在收到 notif CQE 之后才恢复执行**，外部 API 仅返回发送字节数。

这意味着：`co_await async_send_some(zero_copy(...))` 返回时，内核已经释放了对缓冲区的引用——缓冲区只需在 `co_await` 期间（协程挂起时）保持有效，无需在返回后继续存活。

---

## 代码解析

```cpp
auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    // 协程局部变量：协程帧在挂起期间保活，无需声明为全局或静态变量
    const auto message = std::string("Zero-copy message from io_uring SEND_ZC\n");

    for (int i = 0; i < 3; ++i) {
        auto send_result = co_await client.async_send_some(net::zero_copy(message));
        if (!send_result)
            co_return;

        log::info("Sent {} bytes (zero-copy) to {}", *send_result, peer);
    }
}
```

### 关键点

1. **何时使用**
   - 大块、需要在多次发送间复用的缓冲区（文件内存映射、大型消息）
   - 高频发送且缓冲区频繁重用（相同内容广播）

2. **性能优势**
   - 消除内核到网卡的内存复制
   - 在 PCIe 带宽受限的场景下节省 CPU
   - 网卡支持 zero-copy DMA 时效果最佳

---

## 内存安全约束

`co_await` 等到 notif CQE 后才返回，因此缓冲区只需在 `co_await` 调用期间保持有效——**协程内的局部变量天然满足此约束**（协程帧在挂起期间保活所有局部变量）。

```cpp
// ✅ 正确：协程局部变量（协程帧保活，挂起期间不会析构）
auto local_msg = std::string("hello");
co_await socket.async_send_some(net::zero_copy(local_msg));
// co_await 返回时 notif CQE 已到达，内核已释放引用，local_msg 可安全继续使用或析构

// ✅ 正确：全局或静态缓冲区
constexpr std::string_view static_payload = "hello";
co_await socket.async_send_some(net::zero_copy(static_payload));

// ✅ 正确：由 session 生命周期管理的缓冲区
class Session {
    std::vector<char> reusable_buffer_;  // session 析构前保活
};

// ❌ 错误：co_spawn 分离子协程时，提供方可能先于 notif CQE 析构
auto send_detached(net::ip::tcp::socket& socket) -> async::Task<>
{
    auto msg = std::string("hello");
    // send_detached 协程返回后 msg 析构，
    // 但 co_spawn 的子协程仍在等待 notif CQE，缓冲区已悬空！
    async::co_spawn(socket.async_send_some(net::zero_copy(msg)));
    co_return;
}
```

⚠️ 真正危险的场景是将**指向外部内存的裸 span** 传入已分离（detached）的子协程——子协程等待 notif CQE 期间，提供方可能已析构原始缓冲区。

---

## 对比：三种发送方式

| 方式 | 调用 | 内存复制 | 使用场景 |
|------|------|--------|---------|
| 普通发送 | `async_send_some(span)` | 1 (kern) | 一般数据 |
| Scatter-Gather | `async_send_some(sequence)` | 1 (kern) | 多块组合数据 |
| Zero-Copy | `async_send_some(zero_copy(span))` | 0 (DMA) | 大块、重用缓冲区 |

---

## 下一步

恭喜！你已经掌握了 io_uring 异步网络编程的核心模式：

- ✅ 基础异步操作（睡眠、连接、接收）
- ✅ 协程并发与信号处理
- ✅ 流式接收与超时
- ✅ 多块缓冲区与高性能发送
- ✅ 零拷贝优化

后续可探索：
- 混合 UDP/TCP 服务
- TLS/SSL 集成
- 连接池与负载均衡
- 自适应缓冲环大小

👉 返回 [示例导航](README.md)
