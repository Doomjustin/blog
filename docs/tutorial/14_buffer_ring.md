# 5.3 环形缓冲区调优：buffer_ring 容量规划与内存布局

> **前置知识**：本章深化 [2.3 流式读取与底层内存映射](03_receive_stream.md) 中引入的 `buffer_ring` 基础。理解 Provided Buffers 机制是该章的必要前置。
> **源文件**：[tutorial/19_buffer_ring/main.cpp](../../tutorial/19_buffer_ring/main.cpp)
> **下一节**：[6.1 线程模型与局部性](15_threading.md)

---

## 问题：接收侧的内存开销与延迟

[2.3](03_receive_stream.md) 引入了 `receive_stream` 的 Provided Buffers 机制：应用预分配一个内存池，内核直接写入预分配缓冲区，避免每次 recv 前都要分配临时 buffer。

但原始机制有局限：
- **容量规划**：pool 太小会 block I/O（等待应用回收 buffer），太大浪费内存。
- **内存布局**：缓冲区分散在堆上，CPU cache line 未对齐，TLB miss 率高。
- **碎片化**：长连接中，buffer 反复分配回收，堆碎片积累。

**buffer_ring** 是 Linux 5.19+ 引入的优化版本：内核与应用共享一个固定大小的环形缓冲区队列，零分配、零碎片、cache-friendly。

---

## 核心机制

传统 Provided Buffers：

```
应用预分配 pool [buf_0, buf_1, ..., buf_N]
        ↓
内核 peek，选择 buf_3 写入数据
        ↓
应用 poll，从 CQE 知道 buf_3 已填充
        ↓
应用处理数据，归还 buf_3 到 pool
```

**buffer_ring**：

```
环形队列 [buf_0, buf_1, ..., buf_M-1]（大小固定，M = 2^k）
应用维护 tail_idx（下一个待回收的 buffer）
内核维护 head_idx（下一个待分配的 buffer）
        ↓
内核 recv 时，从 ring[head_idx++] 直接写，head_idx 模 M
        ↓
应用处理 CQE 后，push tail_idx++，向内核通知"已回收到这里"
        ↓
内核 recv 时检查 head_idx < tail_idx，确保不覆盖未回收数据
```

---

## 性能特征

| 指标 | 传统 pool | buffer_ring |
|------|----------|-----------|
| 内存分配 | 每个 buffer（应用控制） | 零（fixed ring） |
| 缓冲区查询 | 遍历 list 或索引数组 | O(1) 环形索引 |
| CPU cache 行为 | 分散堆上，likely miss | 连续分配，预热高效 |
| 长连接碎片 | 逐渐积累 | 零碎片 |
| 吞吐量 | baseline | 视负载而定（建议按本文曲线方法实测） |

---

## 容量规划

`buffer_ring` 的大小应满足：

```
M >= (max_concurrent_connections) × (RTT / avg_process_time)
```

例如：
- 1000 并发连接
- RTT = 10 ms
- 应用每次处理耗时 1 ms
- M >= 1000 × (10/1) = 10000

**保守策略**：M = 16384（2^14），每个 buffer 4 KB → ring 占 64 MB 内存。

**激进策略**：M = 4096（2^12），前提是确保应用处理速率 >> 网络输入速率。

---

## 内存对齐与布局

`buffer_ring` 内的缓冲区应该：

1. **页对齐**（推荐）：每个 buffer 头部对齐到 4 KB，充分利用 TLB。
   ```cpp
   struct BufferRing {
       static constexpr size_t BUFFER_SIZE = 4096;  // 页大小
       std::vector<std::byte> storage;              // 连续分配
       // 内部逻辑分成 [buf_0][buf_1]... 每个 BUFFER_SIZE 字节
   };
   ```

2. **NUMA 感知**（多 socket 系统）：在相应 NUMA node 分配，避免跨 socket 访问。

3. **Hugepage 支持**（可选）：用 2 MB hugepage 减少 TLB miss，但需 mlock 权限。

---

## 当前示例代码对应关系

当前 [tutorial/19_buffer_ring/main.cpp](../../tutorial/19_buffer_ring/main.cpp) 采用了针对小包场景的配置：

```cpp
auto chat_bgid = async::this_coroutine::setup_buffer_ring(8192, 256);
```

- `entries=8192`：吸收弹幕类突发
- `size=256`：匹配小包 payload，减少浪费

运行命令：

```bash
./build/tutorial/19_buffer_ring/tutorial.19_buffer_ring
```

本次会话实测输出：

```text
[info] === Demo: Buffer Ring Tuning ===
[info] [Server] 已分配定制化弹幕 Buffer Ring (BGID: 0)
[info] [Server] 开始接收弹幕流...
[info] [Server] 收到弹幕 (8 bytes): 666666!
[info] [Server] 收到弹幕 (19 bytes): 前方高能预警
[info] [Server] 收到弹幕 (15 bytes): 完结撒花~~
```

### 容量-吞吐曲线复现实验

当前示例已固定参数，建议按以下方式做曲线：

1. 仅修改 `setup_buffer_ring(entries, 256)` 的 `entries`。
2. 取 `entries={512,1024,2048,4096,8192}`。
3. 固定消息总量后统计总耗时，按 $throughput=\frac{N}{t}$ 计算吞吐。

---

## 与 Scatter/Gather 和 Zero-copy 的关系

```
接收侧优化链：
buffer_ring（高效内存管理） 
    ↓
2.3 receive_stream（业务逻辑隐藏细节）
    ↓
receive_stream + receive_zero_copy（避免拷贝到应用态）

发送侧优化链：
5.1 Scatter/Gather（多段单 syscall）
    ↓
5.2 Zero-copy 发送（避免 kernel buffer 拷贝）
    ↓
send_zero_copy + buffer_ring 预分配（完整流）
```

三者独立但可组合。通常的优化顺序是：**buffer_ring 容量规划 → 再加 Scatter/Gather → 最后才上 Zero-copy**，因为收益递减且复杂度递增。

---

## 架构诚实

- **内核版本**：buffer_ring 需要 Linux 5.19+。早期版本降级到传统 Provided Buffers 或每次 recv 分配。
- **驱动支持**：io_uring 后端的网卡驱动也需支持。某些旧网卡即使内核新也无法启用。
- **NUMA 影响**：多 socket 系统下，跨 socket 访问吞吐量可能下降 30%。需 NUMA 感知分配。
- **内存锁定**：buffer_ring 一旦分配通常 mlock（防止 page-out），占用系统内存预算。

---

## 本章小结

buffer_ring 通过固定大小的环形队列、零分配策略和 cache-friendly 内存布局，显著提升接收侧吞吐量。容量规划应基于并发连接数、RTT 和应用处理延迟，保守取 16384-65536。内存对齐和 NUMA 感知能进一步优化 TLB 和跨 socket 访问。

> **下一节**：[6.1 线程模型与局部性](15_threading.md) — 从性能微调进阶到多线程部署实践：对称并发、跨线程迁移与局部性优化。
