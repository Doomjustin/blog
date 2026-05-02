在前面的写路径优化里，我们已经把 `writev` 这类“减少 syscall 次数”的手段铺好了。接下来这篇聊另一个方向：**减少数据复制**。

目标很直接：发送大块数据时，尽量不把用户态 buffer 再拷进一份内核缓存。

在 Linux `io_uring` 里，对应能力是 `IORING_OP_SEND_ZC`。

---

### 1. 普通 send 到底慢在哪

普通发送路径里，用户态数据通常会先复制到内核 socket buffer，然后再由驱动发给网卡。简化后是这样：

```text
user buffer
  -> copy_to_kernel
kernel socket buffer
  -> DMA / driver
NIC
```

`SEND_ZC` 试图省掉第一段复制：内核不再做那次 data copy，而是让发送过程直接引用用户态内存。

收益在大包、持续发送时最明显；小包场景下，省下来的 copy 往往抵不过额外的状态管理开销。

---

### 2. zero-copy 真正的难点：完成语义不是“一次 CQE 就结束”

`SEND_ZC` 和普通 `send` 最大区别，不在 API 形态，而在 completion model。

它可能产生两类 CQE：

1. **数据发送结果 CQE**：告诉你这次 send 的返回值（成功字节数或错误）。
2. **notification CQE**（`IORING_CQE_F_NOTIF`）：告诉你内核/驱动已经不再引用这块用户 buffer。

换句话说，`co_await` 返回“发送结果”不代表 buffer 可以马上释放。真正安全释放的时点，是 notification 到来之后。

这就是 zero-copy 最容易踩坑的地方。

---

### 3. 我在接口层怎么把这个约束显式化

如果 zero-copy 和普通发送共用同一个参数类型，调用方很容易忘掉生命周期约束。

所以我这里引入了一个 tag type：`ZeroCopyT`。

```cpp
struct ZeroCopyT {
    std::span<const std::byte> span;
};

template<std::ranges::contiguous_range T>
auto zero_copy(const T& range) -> ZeroCopyT
{
    return { std::as_bytes(std::span{ range }) };
}
```

调用者必须显式写 `zero_copy(buffer)`，这相当于把“我知道这块内存要多活一会儿”写进调用点。

这个小小的类型分流，比文档注释更可靠。

---

### 4. Awaiter 的状态机：只在正确时机结束

对应 awaiter 是 `SendZCAwaiter`。提交时走 `io_uring_prep_send_zc`：

```cpp
void SendZCAwaiter::prepare(::io_uring_sqe* sqe) noexcept
{
    ::io_uring_prep_send_zc(sqe, fd_, buffer_.data(), buffer_.size(), 0, 0);
}
```

完成路径核心逻辑是按 flags 区分 CQE 语义：

```cpp
void SendZCAwaiter::complete(int result, std::uint32_t flags) noexcept
{
    if (!(flags & IORING_CQE_F_NOTIF))
        set_result(result, flags);

    if (!(flags & IORING_CQE_F_MORE)) {
        context().untrack(this);
        if (handle_)
            handle_.resume();
    }
}
```

这里的关键点：

- 带 `IORING_CQE_F_NOTIF` 的 CQE 不更新发送字节数，它只是“buffer 可以回收”的通知。
- 只有不再带 `IORING_CQE_F_MORE` 时，整个 operation 才真正 complete。

也就是说，awaiter 的结束条件不是“收到了某个 CQE”，而是“收到了最后一个 CQE”。

---

### 5. 为什么返回值设计成 `expected<size_t, error_code>`

`await_resume()` 返回：

```cpp
auto await_resume() noexcept -> std::expected<std::size_t, std::error_code>
```

这和普通发送保持一致，业务层不需要为 zero-copy 写另一套错误处理分支。差异被封装在 awaiter 内部状态机。

这也是这一层封装想达成的目标：

- **语义更强**（需要显式 `zero_copy()`）
- **调用更稳**（返回类型与普通发送一致）

---

### 6. 使用时的边界条件

实践里有三条必须记住：

1. `zero_copy()` 传入的底层内存必须在 notification CQE 到来前一直有效。
2. 小消息、高频短连接场景下，zero-copy 不一定比普通 send 更快。
3. 发生错误时也要等 completion 走完整，不要抢先回收 buffer。

示例调用形态：

```cpp
std::string payload = "Zero-copy message from io_uring SEND_ZC\n";
auto result = co_await socket.async_send_some(net::zero_copy(payload));
if (!result) {
    log::error("send_zc failed: {}", result.error());
    co_return;
}
```

---

### 7. 小结

`SEND_ZC` 带来的不是“一个更快的 send 函数”，而是一套不同的生命周期契约：

- 数据什么时候算“发送成功”
- buffer 什么时候算“可以释放”

把这两个时刻混为一谈，zero-copy 基本就会出错。

用 `ZeroCopyT` 在 API 层做显式分流，用 `SendZCAwaiter` 在 completion 层处理双阶段 CQE，这两层配合起来，才是可用的 zero-copy 封装。

[示例代码](../../examples/zero_copy_send/main.cpp)  
[核心实现](../../src/net/send_zc_awaiter.cpp)  
[接口定义](../../src/net/zero_copy.h)
