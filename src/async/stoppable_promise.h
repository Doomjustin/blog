#ifndef BLOG_ASYNC_STOPPABLE_PROMISE_H
#define BLOG_ASYNC_STOPPABLE_PROMISE_H

#include <concepts>
#include <coroutine>
#include <stop_token>
#include <utility>

#include "io_context.h"
#include "this_coro.h"

namespace async {

template<typename T>
concept cancellable = requires(T&& t, IOContext& context) { t.cancel(context); };

template<typename Awaitable>
concept IOAwaitable =
    requires(Awaitable awaitable, std::coroutine_handle<> handle, IOContext& context) {
        { awaitable.await_ready() } -> std::convertible_to<bool>;

        {
            awaitable.await_suspend(handle, context)
        } -> std::convertible_to<std::coroutine_handle<>>;

        { awaitable.await_resume() };
    };

/// @brief 为 Task promise 提供 `stop_token` 与 `IOContext` 注入能力。
struct StoppablePromise {
    std::stop_token stop_token;
    IOContext* context{ nullptr };

    /// @brief 为基于 Awaiter 的对象接入 stop_token 取消传播。
    /// @tparam Awaitable 被包装的 awaitable 类型。
    template<typename Awaitable>
    class StopTokenWrapper {
    private:
        struct CancelFn {
            StopTokenWrapper<Awaitable>* wrapper;

            void operator()()
            {
                if (wrapper) {
                    if constexpr (cancellable<Awaitable>)
                        wrapper->inner_.cancel(wrapper->context());
                    else
                        wrapper->context().cancel(wrapper->inner_);
                }
            }
        };

        Awaitable inner_;
        StoppablePromise* promise_;
        std::optional<std::stop_callback<CancelFn>> stop_callback_;

    public:
        /// @brief 构造取消包装器。
        /// @param[in] awaitable 被包装的 awaitable。
        /// @param[in] promise 当前 Task 的 promise。
        StopTokenWrapper(Awaitable&& awaitable, StoppablePromise* promise)
          : inner_{ std::forward<Awaitable>(awaitable) }
          , promise_{ promise }
        {}

        /// @brief 析构时注销 stop_callback。
        ~StopTokenWrapper()
        {
            stop_callback_.reset();
        }

        /// @brief 访问当前 Task 绑定的 IOContext。
        /// @return IOContext 引用。
        auto context() const noexcept -> IOContext&
        {
            return *promise_->context;
        }

        /// @brief await_ready 委托给 inner awaitable。
        /// @return inner 的 await_ready 结果。
        auto await_ready() const noexcept -> bool
        {
            return inner_.await_ready();
        }

        /// @brief 安装 stop_callback 并委托给 inner await_suspend。
        /// @param[in] handle 当前 coroutine 句柄。
        /// @return inner 返回的下一个恢复句柄。
        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept
            -> std::coroutine_handle<>
        {
            auto inner_handle = inner_.await_suspend(handle, context());

            if (promise_->stop_token.stop_possible())
                stop_callback_.emplace(promise_->stop_token, CancelFn{ this });

            return inner_handle;
        }

        /// @brief 返回 inner awaitable 的恢复结果。
        ///
        /// 取消结果由底层 awaiter 在 completion 中给出（例如
        /// `operation_canceled`），此处不再做额外改写。
        /// @return inner 的 await_resume 结果类型。
        auto await_resume() noexcept
        {
            return inner_.await_resume();
        }
    };

    /// @brief 根据 awaitable 类型注入 coroutine 环境并适配取消传播。
    /// @tparam Awaitable 输入 awaitable 类型。
    /// @param[in] awaitable 输入 awaitable。
    /// @return 适配后的 awaiter 或原对象转发结果。
    template<typename Awaitable>
    auto await_transform(Awaitable&& awaitable) -> decltype(auto)
    {
        using Tag = std::remove_cvref_t<Awaitable>;

        if constexpr (std::same_as<Tag, this_coro::context_tag>) {
            struct Awaiter {
                IOContext* io_context;

                /// @brief 读取 context 为同步动作，不应触发挂起。
                constexpr auto await_ready() const noexcept -> bool
                {
                    return true;
                }

                void await_suspend(std::coroutine_handle<> handle) noexcept {}

                auto await_resume() const noexcept -> IOContext&
                {
                    return *io_context;
                }
            };

            return Awaiter{ context };
        }
        else if constexpr (std::same_as<Tag, this_coro::stop_token_tag>) {
            struct Awaiter {
                std::stop_token token;

                /// @brief 读取 stop_token 为同步动作，不应触发挂起。
                constexpr auto await_ready() const noexcept -> bool
                {
                    return true;
                }

                void await_suspend(std::coroutine_handle<> handle) noexcept {}

                auto await_resume() const noexcept -> std::stop_token
                {
                    return token;
                }
            };

            return Awaiter{ stop_token };
        }
        else if constexpr (IOAwaitable<Awaitable>) {
            return StopTokenWrapper<Awaitable>{ std::forward<Awaitable>(awaitable), this };
        }
        else {
            return std::forward<Awaitable>(awaitable);
        }
    }
};

} // namespace async

#endif // BLOG_ASYNC_STOPPABLE_PROMISE_H