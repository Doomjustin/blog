在上一篇里，我们解决了"怎么把数据发出去"。这一篇转到读路径：把 `recv_multishot` 接进来，减少高频读取时的重复提交。

如果沿用 `async_read_some`，每次读取都要先准备一块可写 buffer，再提交一次 `recv`，等 CQE 回来后再决定要不要继续下一次。这个模型本身没错，但放到 Echo、代理、网关这类长连接里，会很快变成重复劳动：准备 buffer、提交 SQE、等待 CQE、再补上下一次读取。

`recv_multishot` 能直接解决这件事：一次提交，对应后续多次完成。

这里我们需要考虑以下问题：

1. buffer 不再由调用方临时传入，那谁来持有和归还。
2. 事件循环怎么判断一笔 multishot 操作到底算不算完成。
3. 用户最终该怎么使用这套能力，才不会再次暴露底层细节。

---

### 1. 从 multishot 目标开始

我们不是要推翻 `read_some`，而是要消掉高频场景里的重复提交。

传统的 `async_read_some` 模型当然能用，但它要求调用方持续参与底层步骤：

1. 业务层必须参与缓冲区管理。
2. 每次读都要重新构造一笔新的 SQE。
3. 热连接上的收包循环会不断重复同样的模板代码。

`recv_multishot` 正好对准了这一点：把“多次提交”收敛成“一次提交，多次完成”。

问题是，当它真的接进现有框架后，复杂度会立刻转移：调用方仍要处理 buffer 生命周期，`IOContext` 里也没有稳定位置安放 buffer ring 这类长寿命资源。到这一步，先要解决的就不是 awaiter 写法，而是职责边界。

于是读路径先做职责重划：

1. 事件循环继续负责 SQE/CQE 的调度。
2. buffer ring 交给上下文长期持有和回收。
3. 连接对象只暴露消费接口，而不是让业务层直接碰 buffer slot。

对应地，原本比较散的 `IOContext` 实现也被重新整理：和 ring 驱动、唤醒、CQE 分发有关的逻辑收进 `Scheduler`；和 buffer ring 建立、默认组选择、槽位归还有关的逻辑收进 `BufferRingGroup`；`IOContext` 自己只保留对外协调接口和 work tracking。

这不是为了“代码好看”，而是为了把复杂度放回正确位置。继续把这些逻辑混在一个类里，后面无论接收流、buffer 池复用还是取消清理，都会越来越难理顺。

如果不做这一步，代码大概会往一个很尴尬的方向长：`IOContext` 一边负责 `submit_and_wait`、wakeup、CQE 分发，一边还要记住有哪些 buffer group、默认组是谁、slot 该怎么归还；socket 层再额外拿着 `bgid` 和 `bid` 到处传。代码当然还能跑，只是边界会越来越散：事件循环知道太多 buffer 细节，socket 接口又背着一堆本不该暴露出来的资源管理问题。

---

### 2. `IOContext` 为什么必须先重构

直接把 multishot 逻辑塞进原有上下文，会把“调度职责”和“资源职责”搅在一起。现在的 `IOContext` 把这两类职责拆开：

```cpp
class IOContext {
private:
    class Scheduler {
        // ring 初始化、SQE 获取、submit_and_wait、wakeup
    };

    class BufferRingGroup {
        // setup buffer ring、release buffer slot、default bgid
    };

    Scheduler scheduler_;
    BufferRingGroup buffers_;
    std::size_t outstanding_works_{ 0 };
    std::atomic<bool> should_stop_{ false };
};
```

`Scheduler` 和 `BufferRingGroup` 的职责本来就不是一回事。

`Scheduler` 关注的是"这一次事件循环怎么跑"：

1. 初始化和销毁 `io_uring` ring。
2. 提供 SQE。
3. `submit_and_wait` 后遍历 CQE，分发给对应 `Operation`。
4. 通过 `eventfd` 做跨线程 stop 唤醒。

尤其是第 3 点，在引入 multishot 之后已经不能再按"一个 CQE 对应一个完整操作"来理解了。`Scheduler::schedule()` 现在必须识别 `IORING_CQE_F_MORE`，只有在 multishot 真正结束时才能把这笔 work 从计数里扣掉。这意味着 `recv_multishot` 不只是给 socket 层加了个新能力，它也顺手改写了事件循环对"完成"这件事的理解。

而 `BufferRingGroup` 关心的是另一件事："有哪些 buffer 组被长期注册在内核里，以及它们怎么被重复借出和归还"。这部分如果混进 `Scheduler`，后者就会一边跑 CQE 批调度，一边操心 buffer 池资源生命周期，边界很快就会糊掉。

接口本身也能看出这种分层：

```cpp
auto setup_buffer_ring(unsigned entries, unsigned size) -> unsigned
{
    return buffers_.setup(scheduler_.ring(), entries, size);
}

void release_buffer_ring(unsigned bgid, unsigned bid)
{
    buffers_.release(bgid, bid);
}
```

`IOContext` 在这里做的事情很克制：buffer ring 的建立确实需要底层 ring 句柄，但建立完成之后，后续使用者不必再感知这些细节。

有了这层结构，`ReceiveStream` 才真正有了落脚点。否则它一边要操心 multishot 请求，一边还得自己解决 buffer group 的分配和归还，那就不是 socket 接口该承担的事情。

它在 `StreamSocket` 上的入口很轻：

```cpp
auto receive_stream() -> ReceiveStream<Context>
{
    auto default_bgid = this->context().default_buffer();
    if (!default_bgid)
        throw std::runtime_error{ "No default buffer ring available for receive stream" };

    return ReceiveStream<Context>{ this->context(), this->native_handle(), *default_bgid };
}
```

`ReceiveStream` 不是独立工作的，它默认依赖 `IOContext` 里已经准备好的 buffer ring。在当前实现里，第一次 `setup_buffer_ring()` 创建出来的组会自动成为默认组，所以 demo 里只做一次初始化就够了。

---

### 3. 重构落地：把 buffer ring 收进 `IOContext`

buffer ring 进入 `IOContext` 之后，下一步才轮到它自己的实现。

`BufferRingGroup::setup()` 做了三件事：分配一整块连续内存、向内核注册一个 buf ring、把每个 slot 预先填进 ring。

```cpp
auto IOContext::BufferRingGroup::setup(::io_uring* ring, unsigned entries, unsigned size) -> unsigned
{
    auto bgid = next_bgid_++;
    auto& buffer_ring = group_[bgid];

    buffer_ring.size = size;
    buffer_ring.entries = entries;
    buffer_ring.mask = ::io_uring_buf_ring_mask(entries);

    const auto alloc_size = static_cast<std::size_t>(entries * size);
    buffer_ring.base_address = memory_resource_->allocate(alloc_size, ALIGNMENT);

    int res = 0;
    buffer_ring.buffer = ::io_uring_setup_buf_ring(ring, entries, bgid, 0, &res);

    auto* base = static_cast<std::byte*>(buffer_ring.base_address);
    for (unsigned i = 0; i < entries; ++i)
        ::io_uring_buf_ring_add(buffer_ring.buffer, base + i * size, size, i, buffer_ring.mask, i);

    ::io_uring_buf_ring_advance(buffer_ring.buffer, entries);
    buffer_ring.tail = entries;

    if (!default_buffer_bgid_)
        default_buffer_bgid_ = bgid;

    return bgid;
}
```

这部分有三个会直接影响行为的选择。

第一，所有 buffer slot 放在一整块连续内存里，而不是单独 `new` 一堆小块。这么做不是图省事，而是因为 buf ring 的典型使用方式本来就是"固定大小、重复借用、按槽位编号回收"。连续布局让 `bid -> 地址` 的映射退化成简单的指针偏移，后面 CQE 回来时只要 `base + bid * size` 就能找回数据。

第二，`bgid` 的管理被封装在 `BufferRingGroup` 内部。调用方只拿到一个逻辑编号，不直接接触底层注册细节；默认组也在这里顺手建立起来，方便 socket 层提供一个无参的 `receive_stream()`。

第三，归还路径同样应该放在这里，而不是散在各处调用 `io_uring_buf_ring_add`。因为释放 slot 本质上是 buffer ring 自己的状态变更，和具体是哪条连接、哪次读操作用过这个 slot 没关系。

归还逻辑如下：

```cpp
void IOContext::BufferRingGroup::release(unsigned bgid, unsigned bid)
{
    auto& buffer_ring = group_[bgid];
    auto* base = static_cast<std::byte*>(buffer_ring.base_address);
    const int offset = buffer_ring.tail & buffer_ring.mask;

    ::io_uring_buf_ring_add(
        buffer_ring.buffer,
        base + bid * buffer_ring.size,
        buffer_ring.size,
        bid,
        buffer_ring.mask,
        offset
    );

    ::io_uring_buf_ring_advance(buffer_ring.buffer, 1);
    ++buffer_ring.tail;
}
```

一个 slot 被借走时，`bid` 会随 CQE 一起回来；一个 slot 被归还时，`BufferRingGroup` 只需要根据同样的 `bid` 把它重新挂回 ring。这样 buffer 的借出和回收就闭上了环，之后 `PooledBuffer` 才有可能用 RAII 去包装它。

---

### 4. 地基理顺后，再把 multishot 接上

`ReceiveStream` 真正的提交动作在 `arm_operation()` 里：

```cpp
void arm_operation()
{
    if (!operation_)
        operation_ = new MutishotReceiveOperation{ this, context_, bgid_ };

    auto* sqe = context_->sqe();
    ::io_uring_prep_recv_multishot(sqe, fd_, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = bgid_;
    ::io_uring_sqe_set_data(sqe, static_cast<Operation*>(operation_));
    operation_armed_ = true;
}
```

这个提交动作有两个关键信息。

第一，`io_uring_prep_recv_multishot` 只提交一次，但不是只收一次。只要内核认为这个 multishot 请求还有效，后面每次有数据到达，它都可以继续产出新的 CQE。

第二，`IOSQE_BUFFER_SELECT` 告诉内核：接收缓冲区不由这次 SQE 显式给出，而是从 `buf_group` 对应的 buffer ring 里挑一个空闲槽位来写。用户态这次不传 `void* buf`，而是预先把一组固定大小的 buffer 注册给内核，后面由内核按需借用。

CQE 回来时，`res` 是本次收到的字节数，`flags` 里则可能带上两个额外信息：

1. `IORING_CQE_F_BUFFER`：说明这次结果对应某个 buffer ring 里的槽位。
2. `IORING_CQE_F_MORE`：说明这个 multishot 请求还活着，后面还会继续收。

这两个 flag 基本就是 `ReceiveStream` 最关心的信息：一个告诉它"数据落在哪块 buffer 上"，一个告诉它"这条流还要不要继续等下去"。

---

### 5. 最后才是用户侧 API：为什么返回 `PooledBuffer`

如果只是为了把数据交给调用方，返回 `std::span<std::byte>` 看起来已经够了。但对 `ReceiveStream` 来说，这远远不够，因为 span 只是一段视图，不携带任何归还语义。

内核从 buffer ring 里借出的槽位，最终必须还回去，否则 ring 只会越用越少，直到耗尽。这个归还动作不能指望业务层记得手工调用，所以这里必须做成 RAII。

`PooledBuffer` 的职责正是这个：

```cpp
template<typename Context>
class PooledBuffer {
public:
    PooledBuffer(Context& context, unsigned bgid, unsigned bid, std::span<std::byte> buffer)
      : context_{ &context }, bgid_{ bgid }, bid_{ bid }, buffer_{ buffer }
    {}

    ~PooledBuffer()
    {
        release();
    }

    auto data() const noexcept -> std::span<std::byte>
    {
        return buffer_;
    }

private:
    void release()
    {
        if (valid())
            context_->release_buffer_ring(bgid_, bid_);
    }
};
```

调用方拿到的是一块"带归还能力的 buffer 句柄"。它可以读这段数据，也可以把这段数据直接交给后续写操作；一旦这个对象离开作用域，对应槽位就自动回到 buffer ring，可供下一次接收复用。

这也是 `ReceiveStream` 最重要的体验差异：上层拿到的是一块已经装好、生命周期清晰、离开作用域后能自动回收的结果，而不是一段裸内存视图。

---

### 6. `next()` 为什么需要 ready queue

`ReceiveStream` 对外只暴露一个动作：

```cpp
auto next() -> NextAwaiter
{
    return NextAwaiter{ *this };
}
```

`next()` 能不能用得顺，关键在 `NextAwaiter` 和 `ready_results_` 的配合。

```cpp
class NextAwaiter {
public:
    auto await_ready() const noexcept -> bool
    {
        return !stream_.ready_results_.empty();
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        stream_.handle_ = handle;

        if (!stream_.operation_armed_)
            stream_.arm_operation();
    }

    auto await_resume() -> std::expected<PooledBuffer<Context>, std::error_code>
    {
        if (stream_.ready_results_.empty())
            return unexpected_system_error(std::errc::operation_canceled);

        auto result = std::move(stream_.ready_results_.front());
        stream_.ready_results_.pop_front();
        return result;
    }
};
```

这里的 `ready_results_` 不是装饰品，而是必须存在的一层缓冲。

`recv_multishot` 有一个天然特征：协程还没来得及再次 `co_await next()`，它就可能已经连续产出了多个 CQE。如果没有队列，后到的结果只能覆盖先到的结果，或者逼着 `handle_cqe()` 当场恢复协程并同步消化所有数据，这两种都不对。

用 `std::deque<result_type>` 把结果先存起来，问题就顺了：

1. CQE 到达时，先转成 `PooledBuffer` 或 error，推进队列。
2. 如果此刻真的有协程挂在 `next()` 上，就恢复它。
3. 如果协程暂时还没来取，结果就老实待在队列里，等下一次 `await_ready()` 直接命中。

这样一来，`ReceiveStream` 就有了一点异步生成器的感觉：底层持续产出，上层按自己的节奏消费，中间靠一个轻量队列解耦。

---

### 7. CQE 到达后如何变成可消费结果

`handle_cqe()` 做的事情，说到底就是把内核语义翻译成库语义。

```cpp
void handle_cqe(int result, std::uint32_t flags) noexcept
{
    std::optional<PooledBuffer<Context>> pooled_buffer;

    if (flags & IORING_CQE_F_BUFFER) {
        const auto bid = static_cast<std::uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
        auto& buffer_ring = context_->buffer_ring(bgid_);

        auto* base = static_cast<std::byte*>(buffer_ring.base_address);
        std::span<std::byte> data{ base + bid * buffer_ring.size, static_cast<std::size_t>(result) };
        pooled_buffer.emplace(*context_, bgid_, bid, data);
    }

    if (result == -ECANCELED) {
        ready_results_.emplace_back(unexpected_system_error(std::errc::operation_canceled));
    }
    else if (result >= 0) {
        if (pooled_buffer)
            ready_results_.emplace_back(std::move(*pooled_buffer));
        else
            ready_results_.emplace_back(PooledBuffer<Context>{});
    }
    else {
        ready_results_.emplace_back(unexpected_system_error(-result));
    }

    if (handle_) {
        auto handle = std::exchange(handle_, nullptr);
        handle.resume();
    }
}
```

这一步分三层：

第一层，从 `flags` 里提取 `bid`，再结合 `bgid_` 找回 buffer ring，算出这次数据实际落在了哪一段内存上。

第二层，把这段内存包装成 `PooledBuffer`。从这一刻开始，buffer 的归还责任被显式绑定到了对象生命周期上。

第三层，把内核返回码整理成 `std::expected`：正常数据走 value，取消和错误走 error。这样上层的消费方式就统一了。

还有一个细节：`result >= 0` 但没有 `IORING_CQE_F_BUFFER` 时，代码会塞一个默认构造的 `PooledBuffer`。这给上层留出了处理空结果的空间，比如把空 buffer 视为连接关闭。这个约定在 demo 里也直接用到了。

---

### 8. 真正难的是收尾

`ReceiveStream` 最容易写错的，不是提交 multishot，而是收尾。

问题在于：当 `ReceiveStream` 析构时，内核里那笔 multishot 请求不一定已经结束。你当然可以提交一个 cancel SQE，但 cancel 也是异步的，在 cancel 的 CQE 真正回来之前，原来的 multishot CQE 仍然可能再到一次。这个窗口期一旦处理不好，要么 use-after-free，要么 buffer 泄漏。

现在的解法很稳：析构时不直接删 operation，而是先 `detach()`。

```cpp
void destroy() noexcept
{
    if (operation_) {
        operation_->detach();

        auto* sqe = context_->sqe(false);
        ::io_uring_prep_cancel(sqe, static_cast<Operation*>(operation_), 0);
        ::io_uring_sqe_set_data(sqe, nullptr);

        operation_ = nullptr;
    }

    if (context_)
        context_ = nullptr;
}
```

`detach()` 的效果是把 `operation` 和 `ReceiveStream` 本体断开。之后如果晚到的 CQE 还落到这笔 operation 上，`complete()` 会走另一条路径：不再访问已经析构的 stream，而是仅仅把 buffer 槽位补回 buffer ring。

```cpp
void complete(int res, unsigned flags) override
{
    const bool has_more = (flags & IORING_CQE_F_MORE) != 0;

    if (stream_) {
        if (!has_more) {
            stream_->operation_armed_ = false;
            stream_->operation_ = nullptr;
        }

        stream_->handle_cqe(res, flags);
    }
    else if (flags & IORING_CQE_F_BUFFER) {
        // stream 已不存在，只负责把槽位补回 buffer ring
        // ...
    }

    if (!has_more)
        delete this;
}
```

这样一来，`ReceiveStream` 的析构和 multishot 请求的最终死亡就不再需要严格同步，晚到的 CQE 也不会污染用户态状态，只会做最必要的资源回收。

另外，move 构造和 move 赋值里也有一个对应的修正动作：如果 operation 还活着，就把 `operation_->stream_` 改指向新的对象。这保证了 `ReceiveStream` 被移动后，后续 CQE 仍然能回到正确实例上。

---

### 9. 实际用法

在 demo 里，Echo Server 的 session 代码已经很干净了：

```cpp
auto session(ip::tcp::socket<IOContext> client) -> Task<>
{
    auto stream = client.receive_stream();

    while (true) {
        auto read_result = co_await stream.next();
        if (!read_result) {
            spdlog::warn("Failed to read from client {}: {}",
                         client.native_handle(),
                         read_result.error().message());
            co_return;
        }

        auto received = read_result->data();
        if (received.empty()) {
            spdlog::info("Client {} disconnected", client.native_handle());
            co_return;
        }

        auto write_result = co_await timeout(async_write(client, received), 1ms);
        if (!write_result)
            co_return;
    }
}
```

这段代码最直观的变化是：读路径里已经看不到"准备 buffer -> 提交 recv -> 处理 buffer 生命周期"这些样板动作了。业务协程眼里只剩一件事：下一块数据什么时候到。

这正是 `ReceiveStream` 的价值。它不是单纯给 `recv_multishot` 套了个 awaiter，而是顺手把 buffer ring、生命周期、结果排队和析构期收尾这些麻烦一起包掉了。上层得到的是一个可以连续 `next()` 的接收流，底层保留的则是 io_uring 多次投递的吞吐优势。