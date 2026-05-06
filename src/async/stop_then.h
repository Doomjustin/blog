#ifndef BLOG_ASYNC_STOP_THEN_H
#define BLOG_ASYNC_STOP_THEN_H

#include <coroutine>
#include <expected>
#include <functional>
#include <optional>
#include <stop_token>
#include <system_error>

#include <operation.h>
#include <post.h>
#include <single_operation.h>

namespace async {

template<single_shot_operation Op>
class StopTokenWrapper: public CancelableOperation {
public:
    using resume_type = typename Op::resume_type;

    StopTokenWrapper(Op&& op, std::stop_token token)
      : inner_{ std::forward<Op>(op) },
        stop_token_{ std::move(token) }
    {}

    ~StopTokenWrapper() override
    {
        *alive_ = false;
        stop_callback_.reset();
    }

    [[nodiscard]]
    auto await_ready() noexcept -> bool
    {
        pre_stopped_ = stop_token_.stop_requested();
        return pre_stopped_;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;
        inner_.parent = this;

        stop_callback_.emplace(std::move(stop_token_), 
            [alive = alive_, &ctx = inner_.context(), target = &inner_]() mutable -> void {
                post(ctx, [alive = std::move(alive), &ctx, target] {
                    if (*alive) target->cancel();
                });
        });
        
        inner_.await_suspend(handle);
    }

    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (pre_stopped_)
            return std::unexpected(std::make_error_code(std::errc::operation_canceled));

        if (result_ < 0)
            return unexpected_system_error(-result_);

        if constexpr (!std::is_void_v<resume_type>)
            inner_.set_result(result_, flags_);

        return inner_.await_resume();
    }

    auto context() noexcept -> decltype(auto)
    {
        return inner_.context();
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        *alive_ = false;
        stop_callback_.reset();

        result_ = result;
        flags_ = flags;

        this->resume(handle_, result, flags);
    }

    void cancel() noexcept override
    {
        inner_.cancel();
    }

private:
    Op inner_;
    std::stop_token stop_token_;
    std::optional<std::stop_callback<std::function<void()>>> stop_callback_;
    std::shared_ptr<bool> alive_{ std::make_shared<bool>(true) };
    bool pre_stopped_{ false };

    std::coroutine_handle<> handle_;
    int result_{ 0 };
    std::uint32_t flags_{ 0 };
};


template<single_shot_operation Op>
auto stop_then(Op&& operation, std::stop_token token)
{
    return StopTokenWrapper<std::decay_t<Op>>{ std::forward<Op>(operation), std::move(token) };
}

} // namespace async

#endif // BLOG_ASYNC_STOP_THEN_H