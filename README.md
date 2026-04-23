# xin's blog - io_uring + C++20 协程实现库

一个学习记录：基于 Linux io_uring 和 C++20 协程的异步库实现。

## 文档

- [01_基础骨架与Awaiter机制](docs/01_基础骨架与Awaiter机制.md)
- [02_模块解耦与完备的退出机制](docs/02_模块解耦与完备的退出机制.md)
- [03_基于链式请求的零开销超时机制](docs/03_基于链式请求的零开销超时机制.md)
- [04_核心IO操作的协程实现](docs/04_核心IO操作的协程实现.md)
- [05_Protocol与Endpoint的封装](docs/05_Protocol与Endpoint的封装.md)
- [06_socket层次化封装](docs/06_socket层次化封装.md)
- [07_实现Acceptor](docs/07_实现Acceptor.md)