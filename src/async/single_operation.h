#ifndef BLOG_ASYNC_SINGLE_OPERATION_H
#define BLOG_ASYNC_SINGLE_OPERATION_H

#include <expected>

#include <exceptions.h>
#include <io_context.h>
#include <operation.h>

namespace async {

// 所有的awaiter都要能被结构化语义化，所以都要满足这个约束
template<typename Derived, typename ResumeType>
class SingleOperation: public CancelableOperation {
public:
    using resume_type = ResumeType;
    using context_type = IOContext;

    ~SingleOperation() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        static_cast<Derived*>(this)->handle_ = handle;

        if (auto* sqe = context_.sqe()) {
            static_cast<Derived*>(this)->prepare(sqe);
            ::io_uring_sqe_set_data(sqe, this);

            context_.track(this);
            return true;
        }

        error_code_ = EAGAIN;
        return false;
    }

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        return static_cast<Derived*>(this)->result();
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        context_.untrack(this);

        if (result < 0) 
            error_code_ = -result;
        else
            static_cast<Derived*>(this)->set_result(result, flags);

        this->resume(handle_, result, flags);
    }

    auto context() noexcept -> context_type&
    {
        return context_;
    }

protected:
    context_type& context_;
    std::coroutine_handle<> handle_{ nullptr };
    int error_code_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_SINGLE_OPERATION_H