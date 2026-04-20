#ifndef BLOG_READSOME_AWAITER_H
#define BLOG_READSOME_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>
#include <utility>

#include <liburing.h>

#include "exceptions.h"
#include "operation.h"

template<typename Context>
class ReadSomeAwaiter: public Operation {
public:
    using resume_type = std::size_t;

    ReadSomeAwaiter(Context& context, int fd, std::span<std::byte> buffer)
      : context_{ context }, fd_{ fd }, buffer_{ buffer }
    {}

    [[nodiscard]]
    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        handle_ = handle;

        auto* sqe = context_.sqe();

        prepare(sqe);
        ::io_uring_sqe_set_data(sqe, this);
    }

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return unexpected_system_error(error_code_);

        return byte_read_;
    }

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_recv(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result >= 0)
            byte_read_ = static_cast<std::size_t>(result);
        else
            error_code_ = -result;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

    auto context() noexcept -> Context& { return context_; }

private:
    Context& context_;
    int fd_;
    std::span<std::byte> buffer_;

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t byte_read_{ 0 };
    int error_code_{ 0 };
};

#endif // BLOG_READSOME_AWAITER_H