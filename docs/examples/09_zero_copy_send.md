# 09. 零拷贝发送（Zero-Copy Send）

## 先看运行效果

```bash
# Terminal 1: Start zero-copy server
$ ./build/demo/example.zero_copy_send 8081
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

**代价**：缓冲区必须保持有效，直到网卡完成发送。

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
| 1 | 发送字节数 | 0 | 发送操作完成 |
| 2 | - | `IORING_CQE_F_NOTIF` | 网卡通知：缓冲区安全释放 |

库内部处理这两个 CQE，外部 API 仅返回发送字节数。

---

## 代码解析

```cpp
auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    for (int i = 0; i < 3; ++i) {
        // payload 是全局 const 字符串，保证在整个服务器生命周期内有效
        auto send_result = co_await client.async_send_some(net::zero_copy(payload));
        if (!send_result)
            co_return;

        log::info("Sent {} bytes (zero-copy) to {}", *send_result, peer);
    }
}
```

### 关键点

1. **缓冲区生命周期**
   - `payload` 必须保活至第二个 CQE 返回
   - 本例中 `payload` 是全局 `const std::string_view`，安全

2. **何时使用**
   - 大块、长期有效的缓冲区（文件内存映射、大型消息）
   - 高频发送且缓冲区频繁重用（相同内容广播）
   - **不适合**：短期栈缓冲区、局部 `std::vector`

3. **性能优势**
   - 消除内核到网卡的内存复制
   - 在 PCIe 带宽受限的场景下节省 CPU
   - 网卡支持 zero-copy DMA 时效果最佳

---

## 内存安全约束

⚠️ **违反以下任何约束都会导致 UB**：

```cpp
// ❌ 错误：局部缓冲区
auto local_msg = std::string("temporary");
co_await socket.async_send_some(net::zero_copy(async::buffer(local_msg)));
// local_msg 在 co_await 返回后立即销毁，但网卡仍在读取！

// ✅ 正确：全局或长期有效的缓冲区
constexpr auto static_payload = "hello";
co_await socket.async_send_some(net::zero_copy(async::buffer(static_payload)));

// ✅ 正确：由 session 生命周期管理的缓冲区
class Session {
    std::vector<char> reusable_buffer_;  // session 析构前保活
};
```

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
