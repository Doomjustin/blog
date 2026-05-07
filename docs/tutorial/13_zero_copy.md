# 5.2 消除 CPU 拷贝：Zero-copy 发送

> **前置知识**：本章假设你已读完 [5.1（Scatter/Gather）](12_scatter_gather.md)。
> **源文件**：[tutorial/18_zero_copy/main.cpp](../../tutorial/18_zero_copy/main.cpp)
> **下一节**：[5.3 环形缓冲区调优](14_buffer_ring.md)

---

## 问题：大包发送时 CPU 拷贝成本高

当前示例服务端一次发送 10MB 数据。如果用普通发送路径，内核通常需要把用户态数据复制到内核缓冲。zero-copy 的目标是尽量减少这一步拷贝。

---

## 示例代码（当前仓库实现）

```cpp
constexpr std::array<std::byte, 10 * 1024 * 1024> HUGE_FILE{};

auto zc_buffer = net::zero_copy(HUGE_FILE);
auto result = co_await net::send(*client, zc_buffer);
```

客户端循环读取，累计收到字节数：

```cpp
std::size_t total_received = 0;
while (true) {
    auto res = co_await sock.async_receive_some(async::buffer(buf));
    if (!res || *res == 0) break;
    total_received += *res;
}
```

---

## 运行方式

```bash
./build/tutorial/18_zero_copy/tutorial.18_zero_copy
```

实测输出（本次会话）：

```text
[info] === Demo: Zero-copy Send ===
[info] [Server] 开始发送 10MB 巨型文件...
[info] [Server] 10MB 发送完毕。内核已释放对内存的物理锁定 (Notif CQE 已到达)。
[info] [Client] 接收完毕，共收到 10485760 字 节。
```

其中 10485760 字节正好是 10MB，说明发送和接收路径完整闭环。

---

## 性能量化：Zero-copy 带宽提升对比

### 1. 对比维度

- 总耗时（wall clock）
- 发送端 CPU 占用（user+sys）
- 实际吞吐（bytes / seconds）

### 2. 复现实验步骤

1. 基线版：把 [tutorial/18_zero_copy/main.cpp](../../tutorial/18_zero_copy/main.cpp) 里的
   `net::send(*client, zc_buffer)`
   改成普通 buffer 发送（同样 10MB 数据）。
2. Zero-copy 版：保留当前 `net::zero_copy(HUGE_FILE)`。
3. 分别运行并用 `/usr/bin/time -v` 记录耗时和 CPU。

示例命令：

```bash
/usr/bin/time -v ./build/tutorial/18_zero_copy/tutorial.18_zero_copy
```

> 注意：本文不写死固定数字，因为带宽收益与 NIC、内核版本、CPU 拓扑强相关。建议在同机、同负载条件下对比。

---

## 生命周期边界（必须明确）

- `HUGE_FILE` 是静态存储期对象，天然满足发送期间不释放的要求。
- 如果改成局部对象，必须保证其生命周期覆盖整个 `co_await net::send(...)`。
- zero-copy 并不意味着“零等待”；仍需等待对应完成通知（示例日志里的 Notif CQE）。

---

## 本章小结

当前代码给出了最小可运行的 zero-copy 大包发送示例：10MB 一次性发送并正确接收。性能上应通过同机 A/B（普通发送 vs zero-copy）来量化吞吐和 CPU 成本。

> **下一节**：[5.3 环形缓冲区调优](14_buffer_ring.md)
