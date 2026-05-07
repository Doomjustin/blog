#ifndef BLOG_ASYNC_RUN_ON_THREAD_H
#define BLOG_ASYNC_RUN_ON_THREAD_H

#include <atomic>
#include <cerrno>
#include <coroutine>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

#include <io_context.h>
#include <operation.h>
#include <post.h>
#include <this_coroutine.h>

namespace async {

// ----------------------------------------------------------------------------
// from_callback<R>: 框架级桥接器
//
// 用户只提供 "如何注册回调"，桥接层负责：
// 1) 将回调结果切回 IOContext 线程
// 2) 与 timeout/when_any 的 cancel 协同（取消时立即向 parent 回报）
// 3) track/untrack 生命周期配对
// ----------------------------------------------------------------------------
template<typename R>
struct CallbackState {
    IOContext* context_;
    CancelableOperation* owner_;
    CancelableOperation* parent_{ nullptr };
    std::coroutine_handle<> handle_;
    std::optional<R> result_;
    std::atomic_bool claimed_{ false };
    std::atomic_bool tracked_{ false };

    CallbackState(IOContext& context, CancelableOperation* owner)
      : context_{ &context }
      , owner_{ owner }
    {}

    void track() noexcept
    {
        if (!tracked_.exchange(true))
            context_->track(owner_);
    }

    void finish_claimed(int result, std::uint32_t flags) noexcept
    {
        if (tracked_.exchange(false))
            context_->untrack(owner_);

        if (parent_) {
            parent_->complete(result, flags);
            return;
        }

        if (handle_) {
            auto h = std::exchange(handle_, nullptr);
            h.resume();
        }
    }

    void try_finish_from_cancel() noexcept
    {
        if (claimed_.exchange(true))
            return;

        finish_claimed(-ECANCELED, 0);
    }
};

template<typename R, typename Start>
struct CallbackAwaiter : public CancelableOperation {
    using resume_type = std::expected<R, std::error_code>;

    Start start_;
    std::shared_ptr<CallbackState<R>> state_;

    explicit CallbackAwaiter(Start start)
      : start_{ std::move(start) }
      , state_{ std::make_shared<CallbackState<R>>(this_coroutine::context(), this) }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> handle) -> bool
    {
        state_->handle_ = handle;
        state_->parent_ = this->parent;
        state_->track();

        auto state = state_;
        start_([state = std::move(state)](R value) mutable {
            // 先抢占：赢者才有权写结果，输者直接丢弃（cancel 已唤醒协程）
            if (state->claimed_.exchange(true))
                return;

            state->result_.emplace(std::move(value));

            post(*state->context_, [state = std::move(state)]() mutable {
                state->finish_claimed(0, 0);
            });
        });
        return true;
    }

    auto await_resume() -> std::expected<R, std::error_code>
    {
        if (!state_->result_.has_value())
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        return std::move(*state_->result_);
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        if (state_->claimed_.exchange(true))
            return;

        state_->finish_claimed(result, flags);
    }

    void cancel() noexcept override
    {
        // 无法强杀用户线程：取消语义是"停止等待并向 parent 报告取消"。
        state_->try_finish_from_cancel();
    }

    auto context() noexcept -> IOContext&
    {
        return *state_->context_;
    }
};

template<typename R, typename Start>
auto from_callback(Start&& start)
{
    return CallbackAwaiter<R, std::decay_t<Start>>{ std::forward<Start>(start) };
}

// ----------------------------------------------------------------------------
// run_on_thread: 将 CPU 密集型任务卸载到工作线程
//
// 用法：auto result = co_await run_on_thread([] { return heavy_compute(); });
// 返回 std::expected<R, std::error_code>，取消时携带 operation_canceled。
// ----------------------------------------------------------------------------
template<typename F>
auto run_on_thread(F&& f)
{
    using Fn = std::decay_t<F>;
    using R = std::invoke_result_t<Fn&>;

    return from_callback<R>(
        [func = Fn{ std::forward<F>(f) }](std::function<void(R)> cb) mutable {
            std::thread([func = std::move(func), cb = std::move(cb)]() mutable {
                cb(func());
            }).detach();
        }
    );
}

} // namespace async

#endif // BLOG_ASYNC_RUN_ON_THREAD_H
