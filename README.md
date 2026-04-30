# xin's blog - io_uring + C++ Coroutine 实现记录

这是一个围绕 Linux io_uring 与 C++ Coroutine 的学习型项目。

- [01_基础骨架与Awaiter机制](docs/01_基础骨架与Awaiter机制.md)
- [02_模块解耦与完备的退出机制](docs/02_模块解耦与完备的退出机制.md)
- [03_基于链式请求的零开销超时机制](docs/03_基于链式请求的零开销超时机制.md)
- [04_核心IO操作的协程实现](docs/04_核心IO操作的协程实现.md)
- [05_Protocol与Endpoint的封装](docs/05_Protocol与Endpoint的封装.md)
- [06_socket层次化封装](docs/06_socket层次化封装.md)
- [07_实现Acceptor](docs/07_实现Acceptor.md)
- [08_Scatter-Gather_IO与writev实现](docs/08_Scatter-Gather_IO与writev实现.md)
- [09_基于recv_multishot的ReceiveStream](docs/09_基于recv_multishot的ReceiveStream.md)
- [10_去除显式IOContext的架构重构](docs/10_去除显式IOContext的架构重构.md)