在构建了全异步 Echo Server 之后，我们的网络库已经能够处理一来一回的字节流通信。但工程实践中，一个真实的应用往往不会只发送一块连续内存。

考虑一个 HTTP/1.1 响应：响应头（`std::string`）和响应体（`std::vector<char>`）通常分布在两块不相关的内存区域。最朴素的写法是连续调用两次 `async_write_some`，但这意味着两次 `io_uring` 提交、两次协程挂起恢复——这在高并发场景下代价不菲。

更糟糕的是，两次独立的写操作并非原子的。在 Nagle 算法被关闭的情况下（`TCP_NODELAY`），内核可能将头部和体部拆成两个 TCP 报文分别发送，给对端解析器制造不必要的复杂度。

本篇将探讨如何通过 **Scatter/Gather I/O** 机制，在单次系统调用中完成对多块内存的原子性聚合写操作。

---

### 1. 问题根源：内存布局与系统调用边界

标准的 `write(2)` 系统调用接受的是一块连续的 `(void* buf, size_t count)`。要发送分散在多处的数据，要么先 `memcpy` 拼成一块连续缓冲区再发——额外分配加拷贝，延迟上去了，缓存命中率下去了；要么依次对每块数据调用 `write`——多次内核陷入加不可避免的时序问题。

POSIX 的答案是 `writev(2)`：

```c
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
```

调用方构造一个 `iovec` 数组，每个元素描述一块内存区域，内核在内部完成聚合，对外表现为**单次、原子的写操作**。这种模式被称为 **Gather Write**（聚合写），与之对应的 `readv` 称为 **Scatter Read**（分散读），统称 **Scatter/Gather I/O**。

```c
struct iovec {
    void  *iov_base;  // 缓冲区起始地址
    size_t iov_len;   // 缓冲区长度
};
```

---

### 2. Concept 先行：定义 Buffer 序列的语言契约

C++ 标准库没有"一组只读 buffer 的 range"这个抽象，我们用两个 Concept 来定义它：

```cpp
template<typename T>
concept const_buffer = requires(const T& t)
{
    { buffer(t) } -> std::same_as<std::span<const std::byte>>;
};

template <typename T>
concept sequence_buffer = std::ranges::range<T> 
                       && const_buffer<std::ranges::range_reference_t<T>>;
```

`std::string`、`std::vector<char>`、`std::string_view` 等连续存储类型本身就满足 `const_buffer`。因此 `std::vector<std::string>` 或 `std::array<std::string_view, N>` 这类 range 可以直接传给 `async_write_some`，不需要用户手动做任何转换。

---

### 3. 异步实现：`WriteSequenceAwaiter` 的内存安全设计

异步化 `writev` 有一个绕不开的内存安全问题：`iov` 数组的生命周期。

```cpp
template<sequence_buffer Buffer>
class WriteSequenceAwaiter: public Operation {
public:
    WriteSequenceAwaiter(context_type& context, int socket, const Buffer& buffers)
      : context_(context), socket_(socket)
    {
        iov_.reserve(std::ranges::size(buffers));

        for (const auto& chunk : buffers) {
            auto data = buffer(chunk);
            iov_.push_back(::iovec{
                .iov_base = const_cast<std::byte*>(data.data()),
                .iov_len  = data.size()
            });
        }
    }

    void prepare(::io_uring_sqe* sqe) noexcept override
    {
        ::io_uring_prep_writev(sqe, socket_, iov_.data(), iov_.size(), 0);
    }

    // ...
private:
    std::vector<::iovec> iov_;
    // ...
};
```

SQE 提交后，内核在某个未知的时间点才真正执行 I/O，等待期间 `iov` 数组必须保持有效。`WriteSequenceAwaiter` 的做法是**在构造函数中把 `iovec` 元数据全部拷贝进 `iov_` 成员向量**。

这里有一个关键的区分：

* **`iovec` 元数据（指针 + 长度）**：占用空间极小（每个 16 字节），在构造时拷贝，由 Awaiter 对象自身持有。
* **实际的 payload 字节**：不拷贝，`iov_base` 直接指向调用方提供的原始内存。

由于 Awaiter 对象驻留在协程帧中，而协程帧的生命周期与 `co_await` 表达式完全绑定——`co_await` 在整个异步操作期间协程不会销毁帧，因此：

1. `iov_` 向量本身由协程帧持有，直到 `complete()` 恢复后才释放——内核读取 `iov` 数组时内存有效。
2. payload 字节由调用方保证在 `co_await` 期间有效（通过 `@pre` 文档约定），`iov_base` 指针始终合法。

`iov_` 元数据由 Awaiter 自己持有，payload 字节一字节不动——零拷贝，正确性靠协程帧的生命周期保证。

---

### 4. 暴露给用户的 API：`async_write_some` 的重载决议

`WriteSequenceAwaiter` 最终通过 `StreamSocket` 上的重载函数暴露给用户：

```cpp
// 重载一：单块 buffer
auto async_write_some(std::span<const std::byte> buffer) noexcept 
    -> async::WriteSomeAwaiter;

// 重载二：buffer 序列
template<sequence_buffer Buffer>
auto async_write_some(const Buffer& buffer) noexcept 
    -> async::WriteSequenceAwaiter<Buffer>;
```

两个重载共享同一个函数名，编译器依据参数类型自动选择正确的路径：

* 传入 `std::span<const std::byte>`（或满足隐式转换的单一连续 range）→ 走 `WriteSomeAwaiter`，底层是 `io_uring_prep_write`
* 传入满足 `sequence_buffer` 的 range（如 `std::vector<std::string_view>`）→ 走 `WriteSequenceAwaiter`，底层是 `io_uring_prep_writev`


---

### 5. 实战：发送分散的 HTTP 响应

以发送一个 HTTP/1.1 响应为例，头部和体部分别存储在两块独立内存中：

```cpp
auto send_response(net::ip::tcp::socket& conn) -> async::Task<>
{
    std::string header = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n";

    std::string body = "Hello, World!";

    // std::string 直接满足 const_buffer，std::array<std::string, 2> 满足 sequence_buffer
    std::array parts = { header, body };

    // 一次 co_await → 一次 io_uring 提交 → 一次内核 writev
    auto result = co_await conn.async_write_some(parts);
    if (!result) {
        spdlog::warn("send failed: {}", result.error().message());
        co_return;
    }

    spdlog::info("sent {} bytes", *result);
}
```

与朴素的两次 `async_write_some` 调用相比，这里只有**一次协程挂起恢复**，且头部与体部保证在同一次 `writev` 中原子发送——在 `TCP_NODELAY` 的场景下尤为重要。

---

### 6. "写完为止"与"读完为止"：WriteAll 与 ReadAll

`async_write_some` 和 `async_read_some` 只保证**单次内核调用**——它们返回的是"这次实际传输了多少字节"，而非"请求的字节是否全部完成"。在流式协议下，这种部分传输（partial I/O）是完全合法的内核行为。

"写完为止"在网络编程中足够常见，值得在库层面直接封装。`WriteAllAwaiter` 和 `ReadAllAwaiter` 将"重试直到完成"的逻辑封装在 Awaiter 内部，业务层无需感知部分传输的存在：

#### 6.1 WriteAllAwaiter

```cpp
class WriteAllAwaiter: public CancelableOperation {
    // ...
    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (error_code_ != 0 || buffer_.empty())
            resume(handle_, result, flags);   // 完成或出错，恢复协程
        else
            arm_write();                       // 还有剩余，重新提交
    }

    void arm_write() noexcept
    {
        auto* sqe = context_.sqe();
        ::io_uring_prep_send(sqe, socket_, buffer_.data(), buffer_.size(), 0);
        ::io_uring_sqe_set_data(sqe, this);
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result > 0) {
            bytes_written_ += static_cast<std::size_t>(result);
            buffer_ = buffer_.subspan(result);   // 滑动视图，指向剩余部分
        }
        else if (result == 0) {
            error_code_ = ECONNABORTED;          // 对端关闭连接
        }
        else {
            error_code_ = -result;
        }
    }
};
```

`buffer_` 是 `std::span<const std::byte>`，只是视图，不持有数据。每次部分写入后，`set_result` 把视图头部前移；`complete()` 随即检查：还有剩余就调 `arm_write()` 重新提交 SQE，写完了或出错了才调 `resume()` 恢复协程。

这里有个值得留意的细节：重试不能用循环实现。`complete()` 是内核 CQE 的回调路径，不能在里面等待下一次 I/O 完成——唯一的方式是重新提交一个 SQE，让内核完成后再次回调。这也是 `arm_write()` 会在 `complete()` 里再次出现的原因。协程从头到尾只挂起一次，中间所有的重试都在回调链里发生，外面看不到任何细节。

`ReadAllAwaiter` 与之对称，换成 `io_uring_prep_recv` 和可写的 `std::span<std::byte>`。`result == 0` 时返回 `ECONNABORTED`——对端已关闭，已读字节不足期望长度，继续等下去不会再有新数据。

#### 6.2 取消机制：为什么不能用 link_timeout？

在第三篇博客中，我们为一次性的 I/O 操作（`async_read_some`、`async_write_some`）实现了超时机制，其底层是 io_uring 的 **`IOSQE_IO_LINK` + `io_uring_prep_link_timeout`** 方案：将一个 timeout SQE 通过 `IOSQE_IO_LINK` 链接到 I/O SQE 后面，内核自动保证"哪个先完成，另一个被取消"。

但这一方案对 `WriteAllAwaiter` / `ReadAllAwaiter` 完全失效，原因在于它们是**多次提交**的操作——每次部分写入后，`complete()` 回调里会再次调用 `arm_write()` 提交新的 SQE。而 `IOSQE_IO_LINK` 只能链接**相邻的两个** SQE，第一次提交时的链接在第一个 CQE 到达后就已经消耗完毕，无法覆盖后续的重试 SQE。

为此，引入了一套针对多次提交操作的取消机制，核心是 `CancelableOperation` 里的一个 `parent` 指针：

```cpp
struct CancelableOperation : public Operation {
    CancelableOperation* parent{ nullptr };

    void resume(std::coroutine_handle<> handle, int result, std::uint32_t flags) noexcept
    {
        if (parent)
            parent->complete(result, flags);   // 路由给包装层
        else if (handle)
            std::exchange(handle, nullptr).resume();
    }
};
```

`CancelableOperation` 在 `Operation` 基础上新增了一个 `parent` 指针。当操作被一个超时组合器包裹时，`parent` 被设置为该组合器；否则为 `nullptr`，行为与普通 `Operation` 相同。

`WriteAllAwaiter` 继承自 `CancelableOperation`，并在 `complete()` 中通过 `resume()` 而非直接调用 `handle_.resume()`。关键在于 `resume()` 只在**整个操作最终完成或出错**时才被调用——中间每次部分写入完成后，`complete()` 直接调用 `arm_write()` 重新提交，不经过 parent。只有当 `buffer_.empty()`（写完）或 `error_code_ != 0`（出错/被取消）时，才通过 `resume()` 将结果路由出去。这样 parent 只需处理一次最终事件，而不是每次重试。

这段逻辑如果只看文字会比较绕，可以把它压成一个事件时序：

```text
[User Coroutine]          [TimeoutCombinator]             [io_uring]
    |                         |                            |
    |-- co_await timeout() -->|                            |
    |                         |-- submit timer SQE ------->|
    |                         |-- submit write_all SQE --->|
    |                         |                            |
    |                         |<-- CQE(write partial) ---- |  (继续 arm_write)
    |                         |<-- CQE(write done) ---- ---|  (或 error)
    |                         |-- cancel(timer) ---------->|
    |                         |<-- CQE(timer canceled) ----|
    |<------------------------|  resume once               |
```

另一条分支是 timer 先到期：`TimeoutCombinator` 会反向 cancel 当前飞行中的 write/read SQE，同样等两边 CQE 都收干净后再恢复协程。核心目标只有一个：**恢复一次，但把飞行中的请求收尾做完整**。

有了这个基础，`TimeoutCombinator` 就可以实现真正的独立定时器了：

```cpp
template<cancelable_operation Awaiter>
class TimeoutCombinator: public CancelableOperation {
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;

        // 独立提交一个定时器 SQE
        auto* sqe = context().sqe();
        ::io_uring_prep_timeout(sqe, &timeout_, 0, 0);
        ::io_uring_sqe_set_data(sqe, &timer_);

        // 再提交内层操作（内层操作的 parent 已在构造时设为 this）
        awaiter_.await_suspend(handle);
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        // 内层操作（某次重试）先完成了——取消定时器
        if (state_ == State::Pending) {
            state_ = State::AwaiterCompleted;
            auto* sqe = context().sqe(false);
            ::io_uring_prep_cancel(sqe, &timer_, 0);
        }
        if (--pending_cqes_ == 0)
            std::exchange(handle_, {}).resume();
    }

    void on_timer_completed(int result) noexcept
    {
        // 定时器先到——取消内层操作当前正在飞行的 SQE
        if (state_ == State::Pending) {
            state_ = State::TimerCompleted;
            auto* sqe = context().sqe(false);
            ::io_uring_prep_cancel(sqe, &awaiter_, 0);
        }
        if (--pending_cqes_ == 0)
            std::exchange(handle_, {}).resume();
    }
};
```

与 `TimeoutAwaiter` 的 `IOSQE_IO_LINK` 不同，`TimeoutCombinator` 将定时器 SQE 和内层操作 SQE **独立提交**，两者在 io_uring 中是平等的并发请求：

* **内层操作先完成**：通过 `parent->complete()` 进入 `TimeoutCombinator::complete()`，发出 `io_uring_prep_cancel` 取消定时器，等待定时器的 CQE 到达后恢复协程。
* **定时器先到期**：`Timer::complete()` 调用 `on_timer_completed()`，发出 `io_uring_prep_cancel` 取消当前正在飞行的 `awaiter_` SQE，等待其 CQE 到达后以 `timed_out` 恢复协程。

两种情况都要求 **`pending_cqes_`（初值为 2）减到零**才恢复协程——这保证了无论竞争结果如何，所有飞行中的 SQE 的 CQE 最终都被消耗掉，不会残留在完成队列中干扰后续操作。

从调用方视角来看，两套机制的接口完全相同：

```cpp
// async_read_some：单次提交 → 走 TimeoutAwaiter（link_timeout）
auto r1 = co_await timeout(socket.async_read_some(buf), 5s);

// async_read_all：多次提交 → 走 TimeoutCombinator（独立定时器 + cancel）
auto r2 = co_await timeout(socket.async_read_all(buf), 5s);
```

`timeout()` 函数通过两个重载，依据 `single_shot_only_operation` 和 `cancelable_operation` 两个 Concept 在编译期自动分发，调用方无需关心底层选择了哪套机制。

---

#### 6.3 为什么不需要序列版 WriteAll

既然有了序列版 `write_some`，很自然会想到要不要同时提供序列版 `write_all`。

答案是不需要。

`writev` 的关键语义是**原子性**：内核保证整个 `iovec` 数组作为一个整体提交给协议栈。对于流式套接字（`SOCK_STREAM`），内核要么接受全部数据进入发送缓冲区，要么在缓冲区不足时只接受一部分。

但这里有一个根本性的约束：**`writev` 发生部分写入后，没有办法简单地"重试剩余部分"**。每次写入 N 字节后，需要遍历 `iovec` 数组，跳过已完整写入的 chunk，并修剪部分写入那个 chunk 的 `iov_base`/`iov_len`，然后以剩余的 `iovec` 子集重新提交：

```cpp
// 如果要实现序列版 write_all，必须处理这种修剪逻辑
void advance(std::size_t n) {
    while (n > 0 && !iov_.empty()) {
        if (n >= iov_.front().iov_len) {
            n -= iov_.front().iov_len;
            iov_.erase(iov_.begin());          // O(n) erase，或改用 index
        } else {
            auto* base = static_cast<char*>(iov_.front().iov_base);
            iov_.front().iov_base = base + n;
            iov_.front().iov_len -= n;
            n = 0;
        }
    }
}
```

这并非不可实现，但代价是显著的复杂度提升，而实际收益却微乎其微——实践中发送缓冲区充足时 `writev` 极少发生部分写入。

更重要的是，`writev` 的使用场景本身就决定了调用方通常不关心"是否全部写完"：用 `writev` 拼装一个 HTTP 响应，目标是**原子地将头部和体部交给内核**，至于内核何时真正通过 TCP 发出去，不是这一层要操心的。这与 `write_all` 的语义（"确保用户层的所有字节都离开应用缓冲区"）本质上是两个不同的问题。

因此，我们的设计决策是：

| 场景 | API 选择 |
|---|---|
| 一次性发送多块分散内存 | `async_write_some(sequence_buffer)` → `writev` |
| 确保单块内存完整写入 | `async_write_all(span)` → 循环 `send` |
| 既要分散又要保证全部写完 | 希望没有这种需求 |