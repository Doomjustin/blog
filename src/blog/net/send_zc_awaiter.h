#ifndef BLOG_NET_SEND_ZC_AWAITER_H
#define BLOG_NET_SEND_ZC_AWAITER_H

#include <coroutine>
#include <cstddef>
#include <expected>

#include "async/io_context.h"
#include "async/operation.h"

namespace net {

class SendZCAwaiter: public async::Operation {
public:
    using resume_type = std::size_t;
    using context_type = async::IOContext;

    SendZCAwaiter(context_type& context, int fd, std::span<const std::byte> buffer);

    ~SendZCAwaiter() = default;

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept;

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>;

    void prepare(::io_uring_sqe* sqe) noexcept;

    void set_result(int result, std::uint32_t flags) noexcept;

    void complete(int result, std::uint32_t flags) noexcept override;

    auto context() noexcept -> context_type& { return context_; }

private:
    context_type& context_;
    int fd_;
    std::span<const std::byte> buffer_;

    std::coroutine_handle<> handle_{ nullptr };
    std::size_t byte_sent_{ 0 };
    int error_code_{ 0 };
};

} // namespace net

#endif // BLOG_NET_SEND_ZC_AWAITER_H