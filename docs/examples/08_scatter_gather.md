# 08. 分散写入（Scatter-Gather）

## 先看运行效果

```bash
# Terminal 1: Start scatter-gather server
$ example.scatter_gather 8080
[INFO] Scatter-gather server listening on port 8080

# Terminal 2: Connect and send a request
$ telnet localhost 8080
GET / HTTP/1.1
<Enter>

# Terminal 1 output:
[INFO] Client connected: 127.0.0.1:49152
[INFO] Sent 139 bytes to 127.0.0.1:49152
[INFO] Client disconnected: 127.0.0.1:49152

# Terminal 2 output:
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 13

Hello, World!Connection closed by foreign host.
```

---

## 核心理念

HTTP 服务器更常见的组织方式：

- **Status line**：单个字符串
- **Headers**：键值容器（例如 `map<string, string>`）
- **Body**：通常先用字符串（文本响应最常见），发送层再适配为字节视图

最朴素的做法是**连续调用多次 `send()`**：

```
send(status_line) → syscall → kernel
send(header[0])   → syscall → kernel
send(header[1])   → syscall → kernel
...
send(body)        → syscall → kernel
```

代价：
- 多次 `io_uring` SQE 提交
- 多次协程挂起/恢复
- 多次内核上下文切换
- TCP 包可能被拆分（违反协议原意）

**Scatter-Gather I/O** 用**单次 `writev`** 系统调用解决：

```
writev([status_line, serialized_header[0..n], CRLF, body_bytes]) → 单次 syscall
```

---

## 代码解析

### 1. 现实的 HTTP 响应构造

```cpp
struct HttpResponse {
    std::string status_line;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};
```

- **Headers** 在业务层用 map 管理，便于增删改
- **发送前**做一次序列化，避免在业务层拼接字符串
- **Body** 在业务层保留 string，更贴近日常 Web 服务实现
- 进入发送层时统一用 `buffer()` 适配为字节视图

### 2. Scatter-Gather 发送

```cpp
// 先把 map headers 扁平化成 "Key: Value\r\n"
std::vector<std::string> header_lines;
for (const auto& [key, value] : response.headers)
    header_lines.emplace_back(format("{}: {}\r\n", key, value));
header_lines.emplace_back("\r\n");

// 再构建 writev 片段：status + headers + body
std::vector<std::span<const std::byte>> parts;
parts.emplace_back(async::buffer(response.status_line));
for (const auto& line : header_lines)
    parts.emplace_back(async::buffer(line));
parts.emplace_back(async::buffer(response.body));

// Single writev syscall sends all parts atomically
auto send_result = co_await socket.async_send_some(parts);
```

**关键点**：
- `std::vector<std::span<const std::byte>>` 满足 `sequence_buffer` concept
- 单次 `co_await` 触发一次内核 `writev` 调用
- Status + Headers + Body 在 TCP 层面上无法被拆分（原子性）

### 3. 与逐个发送的对比

| 方式 | 系统调用 | 协程挂起 | 优势 |
|------|--------|--------|------|
| 逐个 `send()` | 4 次 | 4 次 | 简单，易调试 |
| `writev()` | 1 次 | 1 次 | 高效，原子性 |

---

## 下一步

下一个示例探讨 **零拷贝发送** — 利用 `io_uring` 的 `IORING_OP_SEND_ZC` 功能，避免从用户空间复制数据到内核 DMA 缓冲区。

👉 [09_zero_copy_send.md](09_zero_copy_send.md)
