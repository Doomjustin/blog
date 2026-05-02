# xin's blog - io_uring + C++ Coroutine 实现记录

这是一个围绕 Linux io_uring 与 C++ Coroutine 的学习型项目。

## 博客

- [01_基础骨架_Awaiter机制](docs/blogs/01_基础骨架_Awaiter机制.md)
- [02_模块解耦_完备退出机制](docs/blogs/02_模块解耦_完备退出机制.md)
- [03_链式请求_零开销超时](docs/blogs/03_链式请求_零开销超时.md)
- [04_核心IO_协程化实现](docs/blogs/04_核心IO_协程化实现.md)
- [05_Protocol_Endpoint封装](docs/blogs/05_Protocol_Endpoint封装.md)
- [06_Socket层次化封装](docs/blogs/06_Socket层次化封装.md)
- [07_Acceptor实现](docs/blogs/07_Acceptor实现.md)
- [08_写路径优化_Scatter-Gather与writev](docs/blogs/08_写路径优化_Scatter-Gather与writev.md)
- [09_读路径优化_recv_multishot与ReceiveStream](docs/blogs/09_读路径优化_recv_multishot与ReceiveStream.md)
- [10_API重构_让用户不再关心IOContext](docs/blogs/10_API重构_让用户不再关心IOContext.md)
- [11_停止机制补完](docs/blogs/11_停止机制补完.md)