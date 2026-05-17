#ifndef BLOG_ASYNC_OPERATION_H
#define BLOG_ASYNC_OPERATION_H

#include <coroutine>

#include <common/common.h>

namespace async {

/// @brief 事件循环使用的基础 Awaiter 节点。
///
/// Awaiter 同时承担 coroutine continuation 与调度节点角色：
/// - `handle` 用于恢复当前 coroutine。
/// - `parent` 用于将 completion 结果向上游 Awaiter 传播。
/// - `id` 由 `IOContext::track` 分配，用于与 CQE `user_data` 关联。
/// - 继承 `MPSCQueueNode`，可用于跨线程投递到执行队列。
struct Operation : public MPSCQueueNode {
    std::coroutine_handle<> handle;
    Operation* parent{ nullptr };

    std::uint64_t id; // 唯一标识当前 Awaiter 实例，会在注册时被赋予值
    int result;
    std::uint32_t flags;

    Operation() = default;

    Operation(const Operation&) = delete;
    auto operator=(const Operation&) -> Operation& = delete;

    Operation(Operation&&) = default;
    auto operator=(Operation&&) -> Operation& = default;

    virtual ~Operation() = default;

    /// @brief 处理 completion 并恢复 coroutine。
    /// @param[in] result completion 的结果码（与 io_uring CQE `res` 语义一致）。
    /// @param[in] flags completion 的附加标志（与 io_uring CQE `flags` 语义一致）。
    /// @return 无。
    virtual void resume(int result, std::uint32_t flags)
    {

        this->result = result;
        this->flags = flags;

        if (parent)
            parent->resume(result, flags);
        else
            std::exchange(handle, nullptr).resume();
    }
};

} // namespace async

#endif // BLOG_ASYNC_OPERATION_H