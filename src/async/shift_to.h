#ifndef BLOG_ASYNC_SHIFT_TO_H
#define BLOG_ASYNC_SHIFT_TO_H

#include <coroutine>
#include <utility>

#include <io_context.h>
#include <operation.h>

namespace async {

namespace detail {

class ShiftToOperation final : public Operation {
public:
    explicit ShiftToOperation(std::coroutine_handle<> handle) noexcept
      : handle_{ handle }
    {}

    void complete(int /*res*/, std::uint32_t /*flags*/) override
    {
        auto handle = std::exchange(handle_, {});
        delete this;
        if (handle)
            handle.resume();
    }

private:
    std::coroutine_handle<> handle_{ nullptr };
};

class ShiftToAwaiter {
public:
    explicit ShiftToAwaiter(IOContext& context) noexcept
      : context_{ &context }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    [[nodiscard]] 
    auto await_suspend(std::coroutine_handle<> handle) const noexcept -> bool
    {
        auto* op = new ShiftToOperation{ handle };
        if (context_->is_owner_thread())
            context_->submit(op);
        else
            context_->post(op);
        return true;
    }

    void await_resume() const noexcept {}

private:
    IOContext* context_;
};

} // namespace detail

inline auto shift_to(IOContext& context) noexcept -> detail::ShiftToAwaiter
{
    return detail::ShiftToAwaiter{ context };
}

} // namespace async

#endif // BLOG_ASYNC_SHIFT_TO_H