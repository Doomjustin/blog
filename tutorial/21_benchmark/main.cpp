#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <thread>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

constexpr int MESSAGE_COUNT = 1'000'000;

// ============================================================================
// 选手 A：传统的 C++ 锁队列 (Mutex + Condition Variable)
// 这是 99% 的 C++ 程序员在没有框架时写跨线程通信的默认方式。
// ============================================================================
template<typename T>
class ClassicMutexQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        cv_.notify_one();
    }

    auto pop() -> T {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

auto run_classic_thread_benchmark() -> void
{
    log::info("=== 选手 A: 传统 std::mutex + std::condition_variable ===");
    
    ClassicMutexQueue<int> queue;
    
    auto start = std::chrono::high_resolution_clock::now();

    // 消费者线程：死等锁，消费 100 万条
    std::thread consumer([&]() {
        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            volatile int val = queue.pop(); // volatile 防止被编译器优化掉
            (void)val;
        }
    });

    // 生产者线程：疯狂加锁，生产 100 万条
    std::thread producer([&]() {
        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            queue.push(i);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    log::warning("[传统锁模型] 发送 {} 条跨线程消息，总耗时: {} ms", MESSAGE_COUNT, ms);
}

// ============================================================================
// 选手 B：我们的 TPC 跨核管道 (ChannelPipe + io_uring 无锁事件循环)
// ============================================================================
auto run_tpc_channel_benchmark() -> void
{
    log::info("\n=== 选手 B: async::ChannelPipe (TPC 无锁架构) ===");
    
    async::IOContext ctx_net;
    async::IOContext ctx_worker;

    // 容量为 1024，自带节点池和批量 Credit 归还机制
    auto [tx, rx] = async::make_channel<int>(1024, ctx_net, ctx_worker);

    auto start = std::chrono::high_resolution_clock::now();

    // 消费者协程 (跑在 Worker 线程)
    auto consumer_coro = [](async::ChannelReceiver<int> rx) -> async::Task<> {
        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            auto val = co_await rx.receive();
            volatile int v = *val;
            (void)v;
        }
    };

    // 生产者协程 (跑在 Net 线程)
    auto producer_coro = [](async::ChannelSender<int> tx) -> async::Task<> {
        for (int i = 0; i < MESSAGE_COUNT; ++i) {
            co_await tx.send(i);
        }
        tx.close();
    };

    // 启动双核物理线程引擎
    async::co_spawn(consumer_coro(std::move(rx)), ctx_worker);
    std::jthread worker_thread([&ctx_worker] { ctx_worker.run(); });

    async::co_spawn(producer_coro(std::move(tx)), ctx_net);
    ctx_net.run();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    log::info("[TPC 协程模型] 发送 {} 条跨线程消息，总耗时: {} ms", MESSAGE_COUNT, ms);
}

} // namespace

int main()
{
    log::info("🚀 开始进行跨线程通信吞吐量极限测试...");
    
    run_classic_thread_benchmark();
    run_tpc_channel_benchmark();
    
    return EXIT_SUCCESS;
}