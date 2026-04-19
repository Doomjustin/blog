### 目标与设计边界
本文旨在实现一个基于 `io_uring` 封装的 C++ 协程网络库。在着手编码前，我们先确立一个严格的设计边界：

**不考虑跨平台，不考虑兼容 epoll 等传统多路复用机制。**

为什么舍弃跨平台等通用性？
一旦引入跨平台封装，不仅维护成本陡增，更关键的是**性能势必要做出妥协**。不同操作系统的异步 API 在机制上存在根本分歧，强行封装通常只能取它们的公共子集，或者在用户态引入额外的抽象层来模拟缺失的语义。无论哪种方式，都会对最终性能造成不可预期的损耗。

在基础设施级别的系统库中，性能是无法在项目后期通过“手法”来弥补的，必须在架构初期就定下基调。因此，我们选择不给自己埋雷，直接将底层与 `io_uring` 强绑定。

### 为什么选择 io_uring？
相比于 epoll，`io_uring` 的心智模型更加契合协程。
epoll 暴露的是 Reactor 模型接口（就绪通知），本质上依然是接近线程回调的处理方式。而 `io_uring` 是标准的 Proactor 模型（完成通知）。C++20 的协程天然就是一个异步操作状态机，也是标准的 Proactor 范式。两者的结合能最大程度地降低封装阻抗，减少无谓的状态转换代码。

### IOContext 是什么？
对于初接触异步网络库的读者，可以简单将 `IOContext` 理解为**事件收割机与协程调度中枢**。

在代码中，你提交的各种异步操作（即 `io_uring` 的 SQE 事件），最终都需要一个统一的执行流去收割它们的完成结果（CQE）。成熟的模式是借鉴 Boost.Asio 的 `io_context` 抽象：通过阻塞调用 `IOContext::run()` 来消耗掉所有已就绪事件，唤醒对应的协程，然后继续等待下一轮事件就绪。

### 构建基础框架
基于 C++ 的 RAII 原则，`IOContext` 的首要任务是管理 `io_uring` 实例的生命周期。

#### 1. 实例初始化
```cpp
int io_uring_queue_init(unsigned entries, struct io_uring* ring, unsigned flags);
```
* `entries`: 提交队列（SQ）的深度。必须是 2 的幂（如 128, 256）。内核会基于此分配共享内存环。
* `ring`: 指向待初始化的实例。成功后，内存映射地址、队列掩码等状态将被写入该结构。
* `flags`: 控制行为的标志位（如启用 `SQPOLL` 消除系统调用）。我们这里默认置 0 即可。

失败时直接返回负值的系统错误码，不依赖全局 `errno`。

#### 2. 实例销毁
```cpp
void io_uring_queue_exit(struct io_uring* ring);
```
该函数负责解除内存映射 (`munmap`)，并关闭 `io_uring` 在内核中对应的匿名文件描述符，防止虚拟内存与文件句柄泄漏。

#### IOContext 资源管理骨架
基于上述 API，我们搭建出 `IOContext` 的核心轮廓。由于该上下文作为核心中枢运转，移动语义会引发悬垂指针等复杂问题，因此我们在设计上严格禁用拷贝与移动。

```cpp
#include <liburing.h>
#include <atomic>

class IOContext {
public:
    explicit IOContext(unsigned entries)
    {
        if (auto res = ::io_uring_queue_init(entries, &ring_, 0); res < 0)
            throw_system_error(-res, "io_uring_queue_init");            
    }

    IOContext(const IOContext&) = delete;
    auto operator=(const IOContext&) -> IOContext& = delete;

    // 为了简化实现，我们不支持移动
    IOContext(IOContext&&) = delete;
    auto operator=(IOContext&&) -> IOContext& = delete;

    ~IOContext()
    {
        ::io_uring_queue_exit(&ring_);
    }

    [[nodiscard]] auto ring() noexcept -> ::io_uring* { return &ring_; }
    [[nodiscard]] auto ring() const noexcept -> const ::io_uring* { return &ring_; }

private:
    ::io_uring ring_;
};
```

### 收割已就绪事件 (CQE)
接下来实现核心引擎 `IOContext::run()`。这涉及三个底层操作流：

1.  **等待事件就绪**：
    `io_uring_submit_and_wait(struct io_uring* ring, unsigned wait_nr);`

    将提交操作和阻塞等待融合成一次系统调用。`wait_nr` 指明线程必须阻塞到至少出现多少个完成事件才唤醒返回。

    **返回值陷阱**：成功时返回的是提交的 SQE 数量，而非完成的 CQE 数量。

2.  **遍历完成队列 (CQ)**：
    `io_uring_for_each_cqe` 是一个纯用户态宏。它通过带有 *Acquire* 语义的内存屏障读取 CQ 尾指针，无锁且零拷贝地遍历就绪事件。
    
    **状态剥离陷阱**：该宏只是只读遍历，不修改内核视角的头部指针。如果仅仅遍历而不推进状态，队列最终会溢出导致 `-EBUSY`。
3.  **确认事件消费**：
    `io_uring_cq_advance(struct io_uring* ring, unsigned nr);`

    修改用户空间的 Head 指针，并通过 *Store-Release* 语义发布给内核，正式确认这些事件已被收割。

#### user_data 与类型擦除
`io_uring_cqe` 结构中包含一个 `__u64 user_data` 字段。当我们在 SQE 中设置它时，内核会原封不动地将其带入 CQE 返回。这使得我们能够将该标识强制转换回 C++ 对象的指针。

为此，我们提供一个 `Operation` 基类，所有协程 Awaiter 都必须继承此接口：

```cpp
struct Operation {
    virtual ~Operation() = default;
    virtual void complete(int res, unsigned flags) = 0;
};
```

#### 优雅的退出：should_stop_ 的无锁设计

为了安全退出事件循环，我们引入 `should_stop_` 变量。即便网络库采用 Core Per Thread 模型，不涉及跨业务线程的同步，但 `stop()` 操作往往是由操作系统的信号处理器（Signal Handler，如处理 Ctrl+C）触发的。信号中断具有强抢占性，因此必须使用 `std::atomic`。

值得注意的是，这里我们**不使用 CAS（Compare-And-Swap）**。由于停止是一个幂等且无条件的覆盖动作，我们完全不关心过去的运行状态。直接使用 `store` 配合最松散的 `std::memory_order_relaxed` 即可。这提供了硬件级别的防数据撕裂保证，同时将同步开销降到了绝对的最低点。

> WARN: 只有这个变量显然是不足以完整实现 stop 功能的，还需要考虑如何取消已经提交但尚未完成的 I/O 请求，以及如何通知正在等待的 run() 方法尽快返回。我们将在未来的版本中逐步完善这个功能。

#### 完整的 run() 实现
结合外部任务追踪机制，事件循环的最终代码如下：

```cpp
class IOContext {
    // ... 构造与析构保持不变 ...

    void run()
    {
        ::io_uring_cqe* cqe{ nullptr };

        while (!should_stop_.load(std::memory_order_relaxed) && outstanding_works_ > 0)
        {
            auto res = ::io_uring_submit_and_wait(&ring_, 1);
            if (res < 0)
                throw_system_error("io_uring_submit_and_wait");

            unsigned head;
            unsigned count{ 0 };

            io_uring_for_each_cqe(&ring_, head, cqe) {
                ++count;

                if (cqe->user_data != 0) {
                    auto* op = static_cast<Operation*>(io_uring_cqe_get_data(cqe));
                    op->complete(cqe->res, cqe->flags);
                }
            }

            if (count > 0) {
                outstanding_works_ -= count;
                ::io_uring_cq_advance(&ring_, count);
            }
        }
    }

    [[nodiscard]] 
    auto sqe() -> ::io_uring_sqe*
    {
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe)
            xin::throw_system_error("io_uring_get_sqe");
        
        add_work();
        return sqe;
    }

    void stop() noexcept { should_stop_.store(true, std::memory_order_relaxed); }

    // 为了搭配co_spawn，需要暴露add_work和drop_work方法
    void add_work() noexcept { ++outstanding_works_; }

    void drop_work() noexcept
    {
        assert(outstanding_works_ > 0);
        --outstanding_works_;
    }

private:
    ::io_uring ring_;
    std::size_t outstanding_works_{ 0 };
    std::atomic<bool> should_stop_{ false };
};
```

### 深入 Awaiter 机制：SleepAwaiter 实践

单有一个 `IOContext` 是跑不起来的，我们需要验证它与 C++20 协程的交互机制。在此，我们实现一个 `SleepAwaiter`，封装 `io_uring` 的 `IORING_OP_TIMEOUT` 定时器。

```cpp
#include <chrono>
#include <coroutine>
#include <expected>
#include <system_error>
#include <utility>

class SleepAwaiter : public Operation {
public:
    template<typename Duration>
    SleepAwaiter(IOContext& context, Duration d) noexcept
      : context_{ context }
    {
        using namespace std::chrono;
        ts_.tv_sec = duration_cast<seconds>(d).count();
        ts_.tv_nsec = duration_cast<nanoseconds>(d % seconds(1)).count();
    }

    [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;
        auto* sqe = context_.sqe();

        // 提交纯超时指令，count 设为 0 表示只受时间触发
        ::io_uring_prep_timeout(sqe, &ts_, 0, 0);
        ::io_uring_sqe_set_data(sqe, this);
    }

    auto await_resume() const noexcept -> std::expected<void, std::error_code>
    {
        // io_uring 中，超时正常结束会返回 ETIME
        if (error_code_ == ETIME || error_code_ == 0)
            return {};
        
        // 其他错误（如 -ECANCELED 被提前强杀）
        return std::unexpected{ std::error_code{ error_code_, std::generic_category() } };
    }

    void complete(int res, [[maybe_unused]] std::uint32_t flags) noexcept override
    {
        error_code_ = -res;

        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

private:
    IOContext& context_;
    struct __kernel_timespec ts_{};
    std::coroutine_handle<> handle_{ nullptr };
    int error_code_{ 0 };
};

template<typename Duration>
auto sleep_for(IOContext& context, Duration duration) noexcept -> SleepAwaiter
{
    return SleepAwaiter{ context, duration };
}
```

#### 零开销生命周期管理
留意 `await_suspend` 中的 `::io_uring_prep_timeout(sqe, &ts_, 0, 0);`。我们将局部对象 `ts_` 的地址交给了内核。在传统的异步回调编程中，这是一个极易触发悬垂指针的致命错误，通常需要用 `std::shared_ptr` 在堆上分配来强行续命。

但在这里，它是**绝对安全的**。因为 `SleepAwaiter` 本身的生命周期被牢牢绑定在了协程帧内部。直到 `complete` 回调中触发 `handle.resume()` 彻底唤醒协程后，该 Awaiter 才会被销毁。协程从语言底层提供了天然的内存安全保障，这也是 C++ 追求零开销抽象的绝佳体现。

#### 测试示例
最后，我们用一段简单的代码来验证整个基建流转：

```cpp
auto demo(IOContext& context) -> Task<void>
{
    using namespace std::chrono_literals;
    
    spdlog::info("before sleep");

    co_await sleep_for(context, 5s);

    spdlog::info("after sleep");
}

int main(int argc, char* argv[])
{
    IOContext context{};
    co_spawn(context, demo(context));

    context.run();
    return EXIT_SUCCESS;
}
```

输出如下，可以看到，5s后再次输出内容，这证明从请求提交、内核响应、上下文分发到协程唤醒的全链路已完全贯通：
```bash
blog.io_context_v1
[2026-04-19 16:18:17.642] [info] before sleep...
[2026-04-19 16:18:22.642] [info] after sleep...
```

[完整代码](../demo/io_context_v1.cpp)