在前面的写路径优化里，我们已经用 `writev` 这类手段减少了系统调用的次数。接下来这一篇要解决另一个方向的开销：**数据复制**。

传统的 `send` 流程里，用户态的数据会先被复制进内核 socket buffer，再由驱动发给网卡。对于大块数据和高频发送，这一次复制本身就是明显的开销。

Linux `io_uring` 的 `IORING_OP_SEND_ZC` 就是为了避免这一步。但要用对它，需要理解它的完成语义——这是大多数人第一次用时最容易出错的地方。

---

### 1. 为什么需要零拷贝发送

普通 `send` 的流程：

```
用户 buffer
  ↓ (内核 memcpy)
kernel socket buffer  
  ↓ (DMA 或驱动)
网卡
```

`SEND_ZC` 想跳过中间那一步复制，直接让驱动从用户态内存读取：

```
用户 buffer
  ↓ (直接 DMA，无 memcpy)
网卡
```

代价呢？驱动访问内存的时间变长了，所以在这期间，**用户态不能释放或改写这块 buffer**。

---

### 2. 两个 CQE：数据结果 vs 内存释放通知

`SEND_ZC` 的完成模式跟普通 `send` 不一样。它会回两个 CQE：

**第一个 CQE**（不带 `IORING_CQE_F_NOTIF`，带 `IORING_CQE_F_MORE`）
- 内核告诉你这次 `send` 的结果：成功了多少字节或者失败原因
- 但驱动仍在使用你的 buffer
- 协程此时**不会**恢复

**第二个 CQE**（带 `IORING_CQE_F_NOTIF`，不带 `IORING_CQE_F_MORE`）
- 内核通知：我已经用完你的这块 buffer，可以释放了
- 协程**在这一刻**恢复执行
- `await_resume()` 返回第一个 CQE 里已经存好的发送结果

所以从协程的角度，返回 = notification 已到 = 内核已停止引用你的 buffer。buffer 完全可以释放或改写。

---

### 3. 用 tag type 在 API 层标记零拷贝意图

如果 `async_send_some` 直接接收普通 buffer 加一个 bool 标志，很容易在调用点看不出端倪：

```cpp
// 危险写法：一眼看不出 buffer 需要特殊处理
co_await socket.async_send_some(payload, true);
```

更好的做法是引入一个 tag type，强制显式说明：

```cpp
struct ZeroCopyT {
    std::span<const std::byte> span;
};

template<std::ranges::contiguous_range T>
auto zero_copy(const T& range) -> ZeroCopyT
{
    return { std::as_bytes(std::span{ range }) };
}

// 调用时意图清晰
co_await socket.async_send_some(net::zero_copy(payload));
```

这样做的好处：

1. **类型强制约束**：传错类型编译就过不了。
2. **调用点意图清晰**：看到 `zero_copy(...)` 立刻知道这块 buffer 要特殊对待。
3. **完成语义差异明确**：零拷贝和普通发送的完成流程完全不同，标签清楚地区分了两条路。

---

### 4. Awaiter 的状态机：按 CQE flags 区分两阶段

对应 `SendZCAwaiter` 的核心就是这个 `complete()` 方法，按照 flags 区分 CQE 的含义：

```cpp
void SendZCAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    // 不带 NOTIF：这是数据发送结果 CQE，保存结果
    if (!(flags & IORING_CQE_F_NOTIF))
        set_result(result, flags);

    // 不带 MORE：标志最后一个 CQE，结束整个操作
    if (!(flags & IORING_CQE_F_MORE)) {
        context().untrack(this);
        if (handle_)
            handle_.resume();
    }
}
```

这里的关键点：

- 第一个 CQE（不带 NOTIF，带 MORE）：存下发送字节数或错误码，不恢复协程
- 第二个 CQE（带 NOTIF，不带 MORE）：跳过结果保存（已有了），恢复协程

时序是这样的：

```
User coroutine              SendZCAwaiter              io_uring kernel
     |                            |                            |
     | co_await async_send_some   |                            |
     | (net::zero_copy(buf))      |                            |
     |------------------------->  |                            |
     |                   await_suspend()                       |
     |                    - prepare SQE                        |
     |                    - track(this)                        |
     |                            | submit & wait              |
     |                            |--------------------------> |
     |  🔄 (suspended)            |                            |
     |                            |                   1st CQE: send result
     |                            | complete(...)              |
     |                 !(NOTIF)✓  | set_result(bytes sent)     |
     |                 (MORE)✓    | (don't resume yet)         |
     |                            |                            |
     |                            |            2nd CQE: notification
     |                            | complete(...)              |
     |                 !(NOTIF)✗  | (skip set_result)          |
     |                 (MORE)✗    | untrack(this)              |
     |                            | handle_.resume()           |
     | 🔄 (awoken)                |                            |
     | auto result =              |                            |
     | await_resume()             |                            |
     | (return byte_sent_)        |                            |
     | <-- proceed                |                            |
```

两个 CQE 都到了，buffer 安全可释放，协程才真正恢复。

---

### 5. 实战边界条件

虽然协程返回时 notification 已到，但从工程实践角度有几点值得注意：

1. **Buffer 生命周期的本质约束**  
   本质上只需要保证 buffer 在协程返回前保持有效。由于协程返回 = notification 已到，这个条件在实际代码中很容易满足。对于栈上的 `std::string`、`std::vector` 或其他作用域内存，这不成问题。只有当 buffer 是通过 `new` 分配且在其他线程被 `delete` 时，才会违反这个约束——但这种情况通常表明应用层本身的内存管理有问题。

2. **内存被占用的时间，比想象中还要长（TCP ACK 陷阱）**  
   第 1 节提到"驱动访问内存的时间变长了"，乍一看像是微秒级的 DMA 操作。但在真实场景中，这个"变长"往往不是驱动的事儿，而是**毫秒到秒级的网络 RTT**。很多网卡驱动和协议栈实现中，内核必须等到对端返回 TCP ACK 确认包，确认数据不需要重传了，才会吐出带 `NOTIF` 的第二个 CQE。这意味着 `SEND_ZC` 的 notification 延迟直接和恶劣的物理网络环境强绑定：丢包、重传、网络拥塞都会拉长等待时间。在一个跨洲际链路上发送，notification 可能要等好几秒，期间内存始终被内核占用。这对内存规划和资源隔离的影响不可忽视。

3. **大文件的"内存锁定"爆炸（RLIMIT_MEMLOCK）**  
   虽然大文件（MB 级）发送时零拷贝有明显优势，但绝对**不能一次性把几个 GB 的大文件全都梭哈给 `SEND_ZC`**。原因是：内核在等待 notification 期间，会把这块物理内存 Pin 住（锁定，防止被 Swap）。如果瞬间提交过大内存，极易触发操作系统的 `RLIMIT_MEMLOCK` 限制导致直接报错，或者把物理内存撑爆。工业界的正确做法是**分片（Chunking）**：用一个 `while` 循环，每次 `co_await socket.async_send_some(net::zero_copy(chunk))` 发送几 MB，等这几 MB 的 notification 回来（协程唤醒）后，再发下一块。这样既能享受零拷贝的收益，又能保持内存占用在可控范围内。

4. **错误也要等 notification**  
   即使第一个 CQE 返回错误，completion 流程也要走完（等 notification）。不要假设出错时内核会跳过 notification。

示例调用（小消息场景）：

```cpp
std::string payload = "Zero-copy message from io_uring SEND_ZC\n";
auto result = co_await socket.async_send_some(net::zero_copy(payload));
if (!result) {
    log::error("send_zc failed: {}", result.error());
    co_return;
}
// 作用域结束时 payload 自动销毁，此时已安全
```

大文件分片示例：

```cpp
std::ifstream file("large_file.bin", std::ios::binary);
const size_t chunk_size = 1024 * 1024;  // 1 MB
std::vector<char> buffer(chunk_size);

while (file.read(buffer.data(), chunk_size)) {
    size_t bytes_read = file.gcount();
    auto result = co_await socket.async_send_some(
        net::zero_copy(std::span(buffer.data(), bytes_read))
    );
    if (!result) {
        log::error("send chunk failed: {}", result.error());
        break;
    }
    // notification 回来，内存可重用于下一块
}
```

---

### 6. 小结

`SEND_ZC` 的核心是一个**两阶段完成模型**：

1. 第一个 CQE：发送是否成功、发了多少字节
2. 第二个 CQE：内核通知"我用完你的 buffer 了"

协程在第二个 CQE 到来时才恢复，这样调用方既得到了发送结果，也确切知道何时可以释放 buffer。

在 API 层用 `ZeroCopyT` tag type 强制显式选择零拷贝，在 awaiter 层按 CQE flags 正确分发两阶段完成，这两层结合才是鲁棒的设计——既不会因为侥幸巧合而偶然正确，也能清晰表达意图。

[示例代码](../../examples/zero_copy_send/main.cpp)  
[核心实现](../../src/net/send_zc_awaiter.cpp)  
[接口定义](../../src/net/zero_copy.h)
