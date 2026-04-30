前几篇一路写下来，我们已经把 `io_uring` 的关键能力都接进了库：awaiter、timeout、socket、acceptor、`recv_multishot`、`ReceiveStream`。功能越来越完整，但也暴露出一个更现实的问题：API 是否还在逼用户理解本不该由他承担的运行时细节。

最典型的细节，就是 `IOContext`。

`IOContext` 在框架内部当然是核心，事件循环、SQE/CQE 调度、work 计数都离不开它。真正让人犹豫的是另一件事: 用户代码里要不要处处显式传 `IOContext&`。

一开始看，显式传参很"透明"，但写着写着就会发现它并没有给业务层带来真正自由。我们想支持的并发模型其实从一开始就是确定的：每个线程一个 context，每个线程各自跑事件循环，线程数量由启动参数决定。既然目标范式是固定的，继续把 context 暴露成日常 API，用户就很容易产生错觉，以为自己在做线程编排，实际上只是重复一个框架本可以帮他完成的路径。

Asio 和 io_uring-cpp 之类的库之所以把 `io_context` / `io_uring_context` 作为显式参数暴露出来，是因为它们需要支持更复杂的并发拓扑——同一进程里可以有多个独立的 context，同一个 socket 可以在不同 context 之间迁移，线程与 context 的对应关系可以由用户自由配置。为了支持这种自由度，context 就必须出现在每一个需要它的地方：socket 构造时要绑定 context，每次异步操作要知道往哪个 context 提交 SQE。这个代价不是设计失误，而是为了满足通用性的合理取舍。

我们的情况不同。这个库从一开始就只打算支持一种并发模型：每线程一个 context，所有线程跑同样的协程入口，由 `async::run` 统一管理。这个约束是主动选择的，不是偷懒，而是认为对于绝大多数网络服务场景，这个模型就已经够用，而且更难被误用。正因为并发模型是确定的，context 就可以从参数里消失，退回到线程局部存储里待命。

这个选择当然有代价。隐藏 context 之后，用户也就失去了"把这个连接交给某个特定线程处理"的能力。当前的连接分发完全依赖内核的 `SO_REUSEPORT`——多个线程各自 accept，内核负责把新连接均匀分配过去。如果业务需要最少连接数、一致性哈希这类应用层调度策略，默认路径就不够用了。

但这不是无路可走。需要自定义负载均衡的用户，可以绕过 `async::run` 的封装，直接操作 `IOContext`——手动创建线程、绑定 context、管理连接归属。这条路更复杂，要求用户真正理解 context 的生命周期和调度语义，但接口是开放的。默认隐藏 context，不是在堵死这条路，而是认为大多数场景不需要走它。

所以这轮重构想解决的，是让用户不再需要关心 `IOContext`。框架内部继续依赖它，只是不再要求用户把它带进日常代码里。

入口上，这个意图非常直接。`async::run` 的实现把范式固定得很彻底：
```cpp
template<typename Awaiter, typename... Args>
void run(std::integral auto thread_count, Awaiter&& awaiter, Args&&... args)
{
    std::vector<std::jthread> threads;
    for (int i = 1; i < thread_count; ++i) {
        threads.emplace_back([awaiter, args...]() mutable -> void
        {
            detail::push(this_coroutine::context());
            co_spawn(std::invoke(awaiter, args...));
            this_coroutine::context().run();
            detail::erase(this_coroutine::context());
        });
    }

    run(std::forward<Awaiter>(awaiter), std::forward<Args>(args)...);
}
```

用户传入的是一个协程函数和它的参数，`run` 内部完成线程创建、context 绑定、事件循环启动、退出后 context 注销这整条链路。不同线程之间没有共享 context，也不需要用户来决定线程与 context 的对应关系——这条路只有一种走法。
这个模式能成立，依赖的是线程局部 context:

```cpp
auto context() -> IOContext&
{
    thread_local auto context = std::make_unique<IOContext>();
    return *context;
}
```

第一次访问时创建，之后同线程复用。这样一来，很多接口就可以把 context 作为默认参数内收。`StreamSocket`、`BasicAcceptor` 已经是这个方向，用户代码自然回到“写网络协程”本身，而不是在每层函数签名里搬运 `IOContext&`。

这一点在调用体验上的变化非常明显。过去写 server 逻辑，常常会先铺一层 context plumbing，再进入 accept/read/write；现在可以直接围绕连接、会话、超时去组织代码。`co_spawn(session(...))` 看起来更轻，不是因为框架放松了约束，而是因为约束被移回了运行时内部。

有了这些铺垫，我们可以给出一个完整的 echo server demo。`echo()` 启动时调用一次 `setup_buffer_ring`，这是给 `recv_multishot` 在内核里注册 buffer ring 的初始化步骤，内部同样通过 `this_coroutine::context()` 拿到当前线程的 context，用户不需要显式传入。之后就是纯粹的业务逻辑：

```cpp
auto echo() -> async::Task<>
{
    async::setup_buffer_ring(128, 4096);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV6::loopback(), 12345 };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    net::ip::tcp::endpoint client_endpoint{};
    while (true) {
        auto client = co_await acceptor.async_accept(client_endpoint);
        if (!client)
            continue;

        async::co_spawn(session(std::move(*client)));
    }
}

auto shutdown_monitor() -> async::Task<void>
{
    async::SignalSet sets{ async::signals::interrupt, async::signals::terminate };
    co_await sets.async_wait();
    async::stop();
}

int main(int argc, char* argv[])
{
    auto worker_count = std::thread::hardware_concurrency() == 0 ? 4 : std::thread::hardware_concurrency();
    async::co_spawn(shutdown_monitor());
    async::run(worker_count, echo);
}
```

`session`、`echo`、`main` 都不持有 `IOContext`，但调度、超时、收发和停机链路一个都没少。业务层看到的是服务器如何处理连接，而不是运行时如何编排 context。

`session()` 更能说明这一点。它直接从 socket 拿 `receive_stream`，在循环里消费数据，写回时套上超时——整个函数签名和函数体里没有 context 参数的任何踪迹：

```cpp
auto session(net::ip::tcp::socket client) -> async::Task<>
{
    auto stream = client.receive_stream();

    while (true) {
        auto read_result = co_await stream.next();
        if (!read_result || read_result->data().empty())
            co_return;

        auto received = read_result->data();

        using namespace std::literals::chrono_literals;
        auto write_result = co_await async::timeout(async::write(client, received), 1ms);
        if (!write_result)
            co_return;
    }
}
```

这是用户不再需要关心 `IOContext` 之后真正的书写状态：从 accept 到 session、从读取到超时写回，context 在这条路上完全透明。

实际跑起来，20 个 worker 线程同时监听，各自持有独立的 context，accept 和 session 由内核按连接分配到不同线程处理：

```
[2026-04-30 15:59:49.301] [9833] [info] Server listening on ::1:12345
[2026-04-30 15:59:49.301] [9832] [info] Server listening on ::1:12345
[2026-04-30 15:59:49.302] [9835] [info] Server listening on ::1:12345
[2026-04-30 15:59:49.301] [9834] [info] Server listening on ::1:12345
...
[2026-04-30 15:59:49.306] [9850] [info] Server listening on ::1:12345
[2026-04-30 15:59:57.419] [9838] [info] Accepted connection from ::1:52762
[2026-04-30 15:59:58.467] [9838] [info] Received 8 bytes from client 64
[2026-04-30 15:59:58.467] [9838] [info] Data: dasfasf
[2026-04-30 15:59:58.980] [9838] [info] Client 64 disconnected
[2026-04-30 15:59:59.950] [9849] [info] Accepted connection from ::1:52770
[2026-04-30 16:00:00.568] [9849] [info] Client 64 disconnected
[2026-04-30 16:00:01.591] [9841] [info] Accepted connection from ::1:52786
[2026-04-30 16:00:02.043] [9841] [info] Client 64 disconnected
```

日志里每行的线程 ID 不同，但业务代码里根本没有出现过"把这个 session 分配给哪个线程"的逻辑——这正是 `SO_REUSEPORT` 加上每线程独立 context 共同决定的结果，不是用户在代码里手工安排的。

`DetachedTask::promise_type` 仍然在内部绑定 context，并在生命周期里维护 work 计数：

```cpp
template<typename Awaitable>
promise_type(Awaitable&& awaitable, IOContext& ctx = this_coroutine::context())
  : context{ &ctx }
{
    context->add_work();
}

~promise_type()
{
    context->drop_work();
}
```

也就是说，调用侧虽然不再显式传 context，但任务归属、退出条件、调度正确性都还在，而且更一致。

到这里，另一个经常被忽略的问题也一起被收口了: 生命周期结束时，谁来负责停机和取消。

如果 context 长期暴露在业务层，停机往往会变成"每处都能 stop 一下"的分散控制，最后很难判断到底谁在主导退出。现在把入口收敛后，这件事也能回到统一语义。`run` 在每条线程上都会向 `detail::active_contexts` 注册当前 context，退出时再注销；`async::stop()` 遍历这个集合，对每个 context 广播停机：

```cpp
void stop()
{
    std::scoped_lock lock{ detail::contexts_mutex };
    std::ranges::for_each(detail::active_contexts,
        [](IOContext* context) { context->stop(); }
    );
}
```

正在阻塞在 `submit_and_wait` 的事件循环会被唤醒，检查 `should_stop_` 后走退出路径；任务侧依赖 work 计数和协程收尾自然回落。`shutdown_monitor()` 只需要等信号、调一次 `async::stop()`，其余全部由框架收尾。这样 stop/cancel 不再是额外补丁，而是和调度模型放在同一条主线上。

用户写网络服务，只需要关心连接、读写和超时，不需要关心背后跑了几个 context、谁在驱动事件循环、停机时谁负责收尾。`IOContext` 还在，只是不再出现在用户需要阅读和维护的代码里。这一步做完之后，`ReceiveStream`、timeout、buffer ring 这些能力继续扩展，API 也不会再被迫回到"先传 context，再谈业务"的旧写法。
