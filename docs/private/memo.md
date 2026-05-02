### 第 1 站：路线 3（极致的零拷贝 Zero-Copy）—— 封神底层架构
* **为什么排第一**：你现在的引擎在“连接调度”上已经达到了 C100K 级别，但数据的收发仍然依赖内核缓冲区拷贝。如果你先写了 HTTP 协议栈（路线 1），再回头来改零拷贝，你会发现 HTTP 层的 Buffer 管理逻辑可能需要大换血。
* **架构收益**：趁着现在还是纯粹的“字节流”阶段，把 `IORING_OP_SEND_ZC` 彻底拿下。这里会有一个非常硬核的技术呼应——零拷贝发送会产生**两个 CQE**（一个表示发送操作完成，一个表示内核不再占用你的内存，可以安全释放了）。这刚好能完美复用你之前在 `TimeoutCombinator` 中练就的“多 CQE 生命周期追踪”绝技！

### 第 2 站：路线 1（HTTP/1.1 协议栈）—— 从字节到语义
* **为什么排第二**：底座完全成型（零拷贝 + 高并发 + 优雅排干）后，你就可以放心地在上面叠加协议状态机了。
* **架构关键点**：引入诸如 `picohttpparser` 这样的 C 语言解析器。你的零拷贝 Buffer 可以直接喂给解析器，通过指针偏移来零内存拷贝地解析出 HTTP Header 和 Body，将纯粹的性能转化为有实际意义的协议吞吐量。

### 第 3 站：路线 2（异步 TLS/SSL）—— 最难的骨头放在中间
* **为什么排第三**：TLS 握手和加密解密是极其消耗 CPU 和状态机管理的。如果在没写 HTTP 之前就写 TLS，你很难直观地测试它。
* **架构预警（黄金建议）**：为了让这一步平滑，你在做【路线 1】的时候，千万不要把 HTTP 解析器死死绑在 `net::ip::tcp::socket` 上！你需要抽象出一个 `AsyncStream` 概念（C++20 Concept 或多态）。这样到了这一步，你只需要实现一个 `TlsStream`，就能让之前的 HTTP 协议栈在明文和密文之间无缝切换。

### 第 4 站：路线 4（Web 框架封装）—— 面向开发者的“精装交房”
* **为什么排最后**：Router、Middleware、`Request/Response` 对象的封装属于上层建筑（DX，Developer Experience）。
* **架构收益**：此时你已经拥有了世界上最快的零拷贝 HTTP/HTTPS 引擎之一。你只需要用 C++ 的 Lambda 表达式和现代模板技术，给它包上一层甜甜的代码糖。最终，其他开发者可以用寥寥几行代码，启动一个碾压 Node.js 和传统 C++ 框架的高性能 Web 服务。

---

**下一步怎么走？**

既然决定了先打通**路线 3（Zero-Copy）**，你需要把目光聚焦到 `io_uring` 的这几个高级特性上：
1. **`IORING_OP_SEND_ZC`**：真正的零拷贝发送。
2. **Notification CQE (通知型 CQE)**：深入理解 `IORING_CQE_F_NOTIF` 标志位。
3. **Fixed Buffers (`IORING_OP_PROVIDE_BUFFERS` / `IORING_REGISTER_BUFFERS`)**：避免每次 I/O 都做内存映射。

准备好迎接 `io_uring` 进阶篇的挑战了吗？我们可以随时从 `IORING_OP_SEND_ZC` 的改造开始！

---

## Examples 建议（从简到难）

### Level 1 — 协程基础
1. `hello_coroutine` — 最小可运行程序：`async::run(task)`，task 里打印一行，理解 `IOContext` 启动/退出。
2. `sleep` — `co_await async::sleep_for(1s)`，展示协程挂起/恢复，不涉及网络。

### Level 2 — 基础 IO
3. `tcp_echo_client` — 连接到服务器，发一条消息，读回 echo，断开。展示 `send` / `receive` 基本用法。
4. `tcp_echo_server` — 单 `acceptor` + `session` 协程，没有超时，逻辑最简。

### Level 3 — 并发与超时
5. `concurrent_tasks` — `async::co_spawn` 多个协程并发跑，展示协程间无锁并发。
6. `timeout_echo_server` — 在 echo server 基础上加 `async::timeout`，演示超时处理模式。

### Level 4 — 流式读取
7. `receive_stream` — 用 `receive_stream` 做流式协议解析，比如按换行分包，展示 `stream.next()` 的迭代模式。

### Level 5 — 高性能路径
8. `scatter_gather_write` — 用 `write_sequence` / `writev` 发送多段 buffer，对比单次 send。
9. `zero_copy_send` — 用 `send_zc` / `send_all_zc`，演示 zero-copy 发送及其生命周期约束。

### Level 6 — 完整应用
10. `chat_server` — 多客户端广播，session 之间共享状态，综合运用前面的机制。
