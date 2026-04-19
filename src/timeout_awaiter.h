#ifndef BLOG_TIMEOUT_AWAITER_H
#define BLOG_TIMEOUT_AWAITER_H

#include <cerrno>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <expected>
#include <utility>

#include <liburing.h>

#include "chrono_duration.h"
#include "exceptions.h"
#include "operation.h"

template<typename T>
concept uring_operation = requires (T& op, ::io_uring_sqe* sqe, std::coroutine_handle<> handle)
{
    typename T::resume_type;

    requires std::is_lvalue_reference_v<decltype(op.context())>;
    op.context();
    op.prepare(sqe);
    op.set_result(0, 0);
    { op.await_resume() } -> std::same_as<std::expected<typename T::resume_type, std::error_code>>;
} && std::derived_from<T, Operation>;


template<uring_operation InnerOperation>
class TimeoutAwaiter: public Operation {
public:
    using resume_type = typename InnerOperation::resume_type;

    template<chrono_duration Duration>
    TimeoutAwaiter(InnerOperation&& operation, Duration timeout)
      : inner_operation_{ std::forward<InnerOperation>(operation) }
    {
        using namespace std::chrono;

        timeout_.tv_sec = duration_cast<seconds>(timeout).count();
        timeout_.tv_nsec = duration_cast<nanoseconds>(timeout % 1s).count();
    }

    [[nodiscard]]
    constexpr auto await_ready() const noexcept
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        handle_ = handle;

        auto* io_sqe = context().sqe();
        auto* timeout_sqe = context().sqe();

        inner_operation_.prepare(io_sqe);
        io_sqe->flags |= IOSQE_IO_LINK;
        ::io_uring_sqe_set_data(io_sqe, this);

        ::io_uring_prep_link_timeout(timeout_sqe, &timeout_, 0);
        ::io_uring_sqe_set_data(timeout_sqe, this);
    }

    auto await_resume() -> std::expected<resume_type, std::error_code>
    {
        if (is_timed_out_)
            return unexpected_system_error(std::errc::timed_out);

        inner_operation_.set_result(result_, 0);
        return inner_operation_.await_resume();
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result == -ETIME)
            is_timed_out_ = true;
        else
            result_ = result;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        if (!has_result_) {
            set_result(result, flags);
            has_result_ = true;
            
            auto handle = std::exchange(handle_, {});
            handle.resume();
        }
    }
    
    auto context() noexcept -> decltype(std::declval<InnerOperation&>().context())
    {
        return inner_operation_.context();
    }

private:
    InnerOperation inner_operation_;
    __kernel_timespec timeout_;

    std::coroutine_handle<> handle_;
    bool has_result_{ false };
    bool is_timed_out_{ false };
    int result_{ -ECANCELED };
};

#endif // BLOG_TIMEOUT_AWAITER_H