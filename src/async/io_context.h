#ifndef BLOG_ASYNC_IO_CONTEXT_H
#define BLOG_ASYNC_IO_CONTEXT_H

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

#include <liburing.h>
#include <sys/eventfd.h>
#include <sys/poll.h>

#include <common/common.h>

#include "operation.h"

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define XIN_TSAN_ENABLED
#endif
#elif defined(__SANITIZE_THREAD__) // 兼容 GCC
#define XIN_TSAN_ENABLED
#endif

#ifdef XIN_TSAN_ENABLED
extern "C" {
void __tsan_acquire(void* addr);
void __tsan_release(void* addr);
}
#define XIN_TSAN_ACQUIRE(ptr) __tsan_acquire((void*)(ptr))
#define XIN_TSAN_RELEASE(ptr) __tsan_release((void*)(ptr))
#else
#define XIN_TSAN_ACQUIRE(ptr)
#define XIN_TSAN_RELEASE(ptr)
#endif

namespace async {

/// @brief 协程恢复执行器。
/// @details 统一调度同线程 dispatch 与跨线程 post 的恢复请求。
class Executor {
private:
    MPSCQueue<Operation> cross_thread_awaiters_;
    std::vector<Operation*> local_awaiters_;

    void process_cross_thread_awaiters() noexcept;

    void process_local_awaiters() noexcept;

public:
    /// @brief 将跨线程 awaiter 投递到 MPSC 队列。
    /// @param[in] awaiter 待恢复的 awaiter，需已携带 result/flags。
    /// @return true 表示队列由空转非空，调用方通常需要触发 wakeup。
    auto post(Operation* awaiter) noexcept -> bool
    {
        XIN_TSAN_RELEASE(awaiter);
        return cross_thread_awaiters_.push(awaiter);
    }

    /// @brief 将同线程 awaiter 放入本地恢复队列。
    /// @param[in] awaiter 待恢复的 awaiter。
    void dispatch(Operation* awaiter) noexcept
    {
        local_awaiters_.push_back(awaiter);
    }

    /// @brief 执行一次恢复调度。
    ///
    /// 顺序为：先处理本地队列，再处理跨线程队列。
    void execute() noexcept
    {
        process_local_awaiters();
        process_cross_thread_awaiters();
    }
};

/// @brief 基于 io_uring 的事件循环上下文。
///
/// 该类型提供：
/// - SQE/CQE 驱动的 I/O completion 调度；
/// - 以 `id` 为键的 pending awaiter 跟踪；
/// - 跨线程取消请求的 owner-thread 串行化提交。
class IOContext {
private:
    /// @brief 跨线程取消请求节点，仅携带 awaiter id。
    struct CancelNode : public MPSCQueueNode {
        std::uint64_t id;

        CancelNode(std::uint64_t id) noexcept
          : id{ id }
        {}
    };

    static constexpr std::uint64_t WAKEUP_MARKER = 1ULL << 63; // 100...0, 用于标记唤醒事件
    static constexpr std::uint64_t CANCEL_MARKER = 1ULL << 62; // 010...0, 用于标记取消事件

    ::io_uring ring_;
    std::atomic<std::thread::id> thread_id_;
    int wakeup_fd_{ -1 };

    std::atomic<std::size_t> tracking_works_{ 0 };
    std::atomic<bool> should_stop_{ false };

    Executor executor_;

    std::uint64_t next_id_{ 1 };
    // WARN: 这里的 unordered_map 可能会成为性能瓶颈，后续可以考虑使用更高效的
    // ID 分配与存储方案，如分段锁定哈希表或 ID 池。
    std::unordered_map<std::uint64_t, Operation*> pending_tasks_;
    MPSCQueue<CancelNode> cancel_queue_;

    /// @brief 生成 awaiter id，保留高两位给内部 marker。
    /// @return 可用于 `io_uring_sqe_set_data64` 的 user_data。
    auto generate_id() noexcept -> std::uint64_t
    {
        return next_id_++ & 0x3FFFFFFFFFFFFFFF; // 保持最高2位为0，避免与特殊标记冲突
    }

    /// @brief 跨线程入队取消请求并唤醒 owner 线程。
    /// @param[in] awaiter 目标 awaiter。
    void enqueue_cancel(Operation* awaiter) noexcept;

    /// @brief 为指定 id 准备 cancel SQE。
    /// @param[in] id 待取消的 awaiter id。
    /// @return true 表示成功拿到 SQE 并完成填充。
    auto prepare_cancel_sqe(std::uint64_t id) noexcept -> bool;

    /// @brief 在 owner 线程尝试取消指定 id。
    /// @param[in] id 待取消的 awaiter id。
    ///
    /// 若 id 已不在 pending 集合，表示任务已完成或已被移除，本次取消会被忽略。
    void cancel(std::uint64_t id) noexcept;

    /// @brief 消费跨线程取消队列并提交 cancel SQE。
    ///
    /// 该函数仅在 owner 线程执行，确保 `ring_` 的提交路径串行化。
    void process_cancel() noexcept;

    /// @brief 为 `wakeup_fd_` 注册 multishot poll。
    void arm_wakeup() noexcept;

    /// @brief 向 wakeup fd 写入事件，用于唤醒 owner 线程。
    void wakeup() const noexcept;

    /// @brief 读取并清空 wakeup fd 的可读状态。
    void resume_wakeup() const noexcept;

    /// @brief 执行一次调度循环。
    ///
    /// 顺序：执行恢复队列 -> 处理取消队列 -> 等待并消费 CQE。
    void schedule();

public:
    /// @brief 构造 IOContext 并初始化 io_uring / wakeup 通道。
    /// @param[in] entries SQ/CQ 初始容量。
    explicit IOContext(unsigned entries = 256);

    ~IOContext();

    /// @brief 进入事件循环直到 tracked work 归零。
    void run();

    /// @brief 请求停止并唤醒事件循环线程。
    void stop() noexcept;

    /// @brief 跨线程投递恢复请求。
    /// @param[in] awaiter 待恢复的 awaiter。
    void post(Operation* awaiter) noexcept;

    /// @brief 同线程投递恢复请求。
    /// @param[in] awaiter 待恢复的 awaiter。
    void dispatch(Operation* awaiter) noexcept
    {
        executor_.dispatch(awaiter);
    }

    /// @brief 追踪一个新的工作项并可选绑定 SQE user_data。
    /// @param[in,out] sqe 若非空则写入 awaiter id 到 user_data。
    /// @param[in] awaiter 若非空则为其分配 id 并加入 pending 集合。
    void track(::io_uring_sqe* sqe = nullptr, Operation* awaiter = nullptr) noexcept;

    /// @brief 取消追踪一个工作项。
    /// @param[in] awaiter 若非空则从 pending 集合移除对应 id。
    void untrack(Operation* awaiter = nullptr) noexcept;

    /// @brief 请求取消一个 awaiter。
    /// @param[in] awaiter 目标 awaiter。
    ///
    /// owner 线程直接提交 cancel；非 owner 线程走 cancel_queue_。
    void cancel(Operation& awaiter) noexcept;

    /// @brief 获取一个可写 SQE，必要时先 submit 以腾出空间。
    auto sqe() noexcept -> ::io_uring_sqe*;

    /// @brief 判断当前线程是否为事件循环 owner 线程。
    auto is_owner_thread() const noexcept -> bool
    {
        return std::this_thread::get_id() == thread_id_.load(std::memory_order_relaxed);
    }

    /// @brief 获取事件循环 owner 线程 ID。
    auto thread_id() const noexcept -> std::thread::id
    {
        return thread_id_.load(std::memory_order_relaxed);
    }

    /// @brief 获取底层 io_uring 只读句柄。
    auto ring() const noexcept -> const ::io_uring*
    {
        return &ring_;
    }

    /// @brief 获取底层 io_uring 可写句柄。
    auto ring() noexcept -> ::io_uring*
    {
        return &ring_;
    }
};

} // namespace async

#endif // BLOG_ASYNC_IO_CONTEXT_H