# 5.1 消除系统调用：Scatter/Gather I/O

> **前置知识**：本章假设你已读完第 4 部分（4.1-4.4）。
> **源文件**：[tutorial/17_scatter_gather/main.cpp](../../tutorial/17_scatter_gather/main.cpp)
> **下一节**：[5.2 消除 CPU 拷贝：Zero-copy 发送](13_zero_copy.md)

---

## 问题：结构化响应为什么常常多次发送

一个典型 HTTP 响应由多段数据组成：header 和 body。朴素写法通常是两次发送：

```cpp
co_await sock.async_send_some(header);
co_await sock.async_send_some(body);
```

这样会产生两次发送路径（通常对应两次发送系统调用路径）。本章示例改为把两段视图一次提交。

---

## 示例代码（当前仓库实现）

核心在服务端这段：

```cpp
std::string header = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n";
std::string body = "Hello, World!";

std::array<std::string_view, 2> buffers = { header, body };
auto result = co_await client->async_send_some(buffers);
```

这不是内存拼接，而是把两个独立 buffer 作为一个序列提交，底层走 gather send 路径。

---

## 运行方式

```bash
./build/tutorial/17_scatter_gather/tutorial.17_scatter_gather
```

实测输出（本次会话）：

```text
[info] === Demo: Scatter/Gather I/O ===
[info] [Server] 监听中，等待客户端连接...
[info] [Server] 客户端已连接，准备使用 Scatter/Gather 发送数据...
[info] [Server] 成功发送 52 字节！零内存拼接 ，零 CPU 拷贝。
```

---

## 性能量化：Scatter/Gather vs 逐段发送

### 1. syscall 数量量化（本示例场景）

- 逐段发送：2 段数据，理论发送路径调用次数 = 2
- Scatter/Gather：2 段数据，发送路径调用次数 = 1

因此本示例在发送侧的调用次数减少为原来的 $\frac{1}{2}$，即减少 50%。

### 2. 建议的复现实验

先保留当前版本（scatter/gather）：

```bash
strace -f -e trace=sendto,sendmsg,writev ./build/tutorial/17_scatter_gather/tutorial.17_scatter_gather
```

再把 [tutorial/17_scatter_gather/main.cpp](../../tutorial/17_scatter_gather/main.cpp) 中的序列发送改成两次单独发送后重编译，再跑同样命令，对比发送调用次数。

---

## 架构边界

- Scatter/Gather 只解决“多段一次提交”，不自动解决生命周期问题；buffer 仍需在 `co_await` 完成前保持有效。
- 该优化主要减少发送路径调用次数；是否带来明显吞吐提升，取决于 payload 大小和网络栈瓶颈。

---

## 本章小结

当前代码已经给出最小可运行的 Scatter/Gather 示例：两段响应一次发送，逻辑更清晰，发送调用次数在本场景下降 50%。

> **下一节**：[5.2 消除 CPU 拷贝：Zero-copy 发送](13_zero_copy.md)
