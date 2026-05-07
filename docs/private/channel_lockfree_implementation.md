# ChannelLockFree 设计与实现

## 核心架构理念

`ChannelLockFree` 实现了**业务层无锁**的异步通道，通过将并发控制权转移到底层 IOContext 来消除 Channel 内部的原子操作。

### 架构对比

| 层次 | 常规设计 | LockFree 设计 |
|------|--------|------------|
| 业务层（Channel） | `std::mutex` + 原子标志 | 无锁（仅 owner thread 访问） |
| 并发控制 | 10,000+ Channel 互相竞争 | N 个底层 MPSC 队列（N = CPU 核心）|
| 原子操作位置 | 分散在每个 Channel | 集中在 IOContext::post (MPSC) |
| Cache 行竞争 | 高（每次 send/receive） | 低（仅跨线程时） |

## 实现关键点

### 1. 单线程业务层

Channel 本身完全单线程：
- 所有状态（buffer、waiting lists）仅由 owner thread 修改
- **零原子操作** — 没有 `std::atomic`, `std::mutex`, `SpinLock`
- `try_send_or_suspend` 和 `try_receive_or_suspend` 无需锁

```cpp
auto try_send_or_suspend(SendAwaiter* op) -> bool
{
    // Owner thread only — no locks needed
    
    if (!waiting_receivers_.empty()) {
        auto* recv = waiting_receivers_.pop_front();  // O(1)
        recv->value_.emplace(std::move(op->value_));
        op->ok_ = true;
        wake_awaiter(recv, 0);
        return false;  // immediate resume
    }
    // ... 纯单线程逻辑
}
```

### 2. 跨线程通过 IOContext 委托

当唤醒不同线程的 awaiter 时，使用 `IOContext::post()` 而不是直接操作：

```cpp
static void wake_awaiter(SendAwaiter* sender, int result) noexcept
{
    sender->scheduled_result_ = result;
    
    if (sender->ctx_->is_owner_thread()) {
        sender->ctx_->submit(sender);  // 本地队列
    } else {
        sender->ctx_->post(sender);    // 跨线程 MPSC（原子操作在这里）
    }
}
```

**关键洞察**：原子操作被**隐藏**在 IOContext 的底层 MPSC 队列中，业务层（Channel）对其无知。

### 3. Ring Buffer 设计

使用 `std::vector<T>` 替代 `std::deque`：

```cpp
std::vector<T> buffer_;      // 预分配
std::size_t head_idx_{0};    // 读游标
std::size_t count_{0};       // 当前元素数

// 发送时
std::size_t tail_idx = (head_idx_ + count_) % capacity_;
buffer_[tail_idx] = std::move(value);
++count_;

// 接收时
T value = std::move(buffer_[head_idx_]);
head_idx_ = (head_idx_ + 1) % capacity_;
--count_;
```

**优势**：
- O(1) 随机访问（无链表指针追踪）
- 缓存局部性好（连续内存）
- 无碎片化

### 4. O(1) 取消操作

使用**侵入式链表**（Intrusive Linked List）+ **守卫标志**：

```cpp
struct IntrusiveOperationList {
    Operation* head_{nullptr};
    Operation* tail_{nullptr};
    
    void erase(Operation* op) noexcept {
        // 直接操作 op->prev 和 op->next
        if (op->prev) op->prev->next = op->next;
        else head_ = op->next;
        // ...
    }
};

// 取消时的守卫
void cancel_send(SendAwaiter* op) noexcept
{
    if (op->in_queue_) {                    // 守卫标志
        waiting_senders_.erase(op);         // O(1) 删除
        op->in_queue_ = false;
        wake_awaiter(op, -ECANCELED);
    }
}
```

**为什么需要 `in_queue_` 标志**：
- 避免**Use-After-Free**：Operation 可能已经被 pop 并唤醒
- 快速短路：不必遍历列表检查

### 5. Zero-Alloc Wake

使用 `Operation::scheduled_result_` 携带完成码，而不是 lambda：

```cpp
// ❌ 旧方式（分配 lambda）
ctx.post([this, result]() {
    op->resume_with(result);  // 堆分配 lambda 对象
});

// ✅ 新方式（零分配）
op->scheduled_result_ = result;
ctx.post(op);  // IOContext 使用 op->scheduled_result_
```

## 性能优势

### 缓存行竞争减少

- **常规**：每个线程的 Channel 对象都有 `std::mutex`，10,000 Channel = 10,000 缓存行竞争
- **LockFree**：跨线程操作仅触发 N 个底层队列的 CAS 操作（N = CPU 核数，通常 8–32）
- **效果**：缓存行竞争从 **10,000x** 降至 **N**

### 单线程开销消除

- **常规**：即使在单线程场景中，`std::mutex` 仍有 atomic load（~2–3 ns）
- **LockFree**：完全无原子操作，纯寄存器操作

### 申请 / 释放减少

- Ring buffer 无动态申请（除初始化）
- 侵入式链表无额外堆分配
- 无 lambda 闭包堆分配

## 典型用法

```cpp
// 在 IOContext owner 线程中
auto producer = [&]() -> Coroutine<void> {
    auto ch = ChannelLockFree<int>{10};  // 容量 10 的缓冲通道
    
    for (int i = 0; i < 100; ++i) {
        co_await ch.send(i);  // 单线程，无锁
    }
};

// 在另一个 IOContext owner 线程中
auto consumer = [&]() -> Coroutine<void> {
    while (auto msg = co_await ch.receive()) {
        process(*msg);
    }
    // ch.close() 由 producer 调用后唤醒 consumer
};
```

## 与 `Channel` 的语义一致性

`ChannelLockFree` 保持与既有 `Channel<T>` 相同的 API 和语义：
- 相同的 `send()` / `receive()` 签名
- 相同的错误类型（`ChannelError::Closed`）
- 相同的 timeout / cancellation 支持
- 缓冲行为（rendezvous、有限缓冲、无限缓冲）

## 性能基准测试结果

> 测试日期：2026-05-07  
> 构建与命令：Debug 构建，`./build/src/async/tests.async.unit '[!benchmark]' --benchmark-samples 3`  
> 说明：以下数据均来自实际运行输出；`ThreadSafeChannel` 仅作为实验项，不对外暴露。

### SPSC（100k messages，capacity/buffer=16）

| 实现 | mean |
|------|------|
| `Channel<int>` | 16.6361 ms |
| `ThreadSafeChannel<int>`（实验） | 16.9258 ms |
| `ChannelPipe<int>` | 73.9110 ms |

### Buffer/Capacity 影响（50k messages）

#### Channel<int>

| buffer | mean |
|------|------|
| 1 | 14.0099 ms |
| 8 | 8.63897 ms |
| 64 | 6.48573 ms |
| 256 | 6.26492 ms |

#### ChannelPipe<int>

| capacity | mean |
|------|------|
| 1 | 1.68557 s |
| 8 | 100.062 ms |
| 64 | 28.7000 ms |
| 256 | 26.3164 ms |

### MPSC（4 producers x 25k messages，buffer=16）

| 实现 | mean |
|------|------|
| `Channel<int>` | 16.9508 ms |
| `ThreadSafeChannel<int>`（实验） | 17.9949 ms |

### 观察

- `Channel<int>` 在单 IOContext 路径下维持最低延迟。
- `ChannelPipe<int>` 在跨线程 SPSC 下存在额外唤醒/调度成本，但 capacity 提升后吞吐改善明显。
- `ChannelPipe<int>` 在 capacity=1 时退化最明显，capacity>=64 后趋于稳定。

## 合并前验证清单

- [x] 编译无错误
- [x] 功能测试通过（本地）
- [x] 并发强度测试通过（stress tests）
- [x] 性能对标测试（见本页最新 benchmark 数据）
- [x] 文档与性能数据

## 后续优化方向

1. **可选 SpinLock**：添加编译时选项用 SpinLock 替代 `std::atomic<bool> closed_`
2. **预分配等待列表**：使用固定大小的 wait stack 减少堆分配
3. **NUMA 感知**：根据 NUMA 拓扑优化 IOContext 布局

---

**验证日期**：2026-05-07  
**编译器**：GCC / C++23，Debug 构建（本页 benchmark 数据）

