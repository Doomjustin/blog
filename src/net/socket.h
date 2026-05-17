#ifndef BLOG_NET_SOCKET_H
#define BLOG_NET_SOCKET_H

#include <cassert>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <common/common.h>

#include "linger.h"
#include "option.h"
#include "query_endpoint.h"

namespace net {

/// @brief 约束可用于 BasicSocket 的协议类型。
///
/// 要求协议类型提供 endpoint 别名，以及 domain/type/protocol 三元组。
/// @tparam T 待检查的协议类型。
template<typename T>
concept socket_protocol = requires(const T& t) {
    typename T::endpoint;

    { t.domain() } -> std::convertible_to<int>;
    { t.type() } -> std::convertible_to<int>;
    { t.protocol() } -> std::convertible_to<int>;
};

/// @brief 面向协议类型的基础 socket 封装。
///
/// 提供 socket 生命周期管理（RAII）、bind、option 读写与 native handle
/// 访问能力。对象可 move，不可 copy。
///
/// @tparam Protocol 协议类型，需满足 socket_protocol。
template<socket_protocol Protocol>
class BasicSocket : public QueryLocalEndpoint<BasicSocket<Protocol>> {
private:
    static constexpr int INVALID_SOCKET = -1;

    int fd_ = INVALID_SOCKET;

    /// @brief 通过协议参数创建底层 socket fd。
    /// @param[in] protocol 协议对象。
    /// @return 成功创建的 fd。
    static auto create(const Protocol& protocol) -> int
    {
        auto res = ::socket(protocol.domain(), protocol.type(), protocol.protocol());
        if (res == -1)
            throw_system_error("Failed to create socket");

        return res;
    }

protected:
    /// @brief 使用已存在 fd 构造 BasicSocket。
    /// @param[in] fd 已创建的 socket fd。
    explicit BasicSocket(int fd)
      : fd_{ fd }
    {}

public:
    using protocol_type = Protocol;
    using endpoint_type = typename Protocol::endpoint;

    /// @brief 获取/设置 SO_ERROR。
    using error = BooleanOption<SOL_SOCKET, SO_ERROR>;

    /// @brief 获取/设置接收缓冲区大小（SO_RCVBUF）。
    using receive_buffer_size = ValueOption<SOL_SOCKET, SO_RCVBUF>;

    /// @brief 获取/设置发送缓冲区大小（SO_SNDBUF）。
    using send_buffer_size = ValueOption<SOL_SOCKET, SO_SNDBUF>;

    /// @brief 获取/设置 linger 行为。
    using linger = LingerOption;

    /// @brief 获取/设置非阻塞标志（O_NONBLOCK）。
    using non_blocking = FlagOption<F_GETFL, F_SETFL, O_NONBLOCK>;

    /// @brief 获取/设置 close-on-exec 标志（FD_CLOEXEC）。
    using close_on_exec = FlagOption<F_GETFD, F_SETFD, FD_CLOEXEC>;

    /// @brief 构造未打开的 socket 对象。
    BasicSocket() = default;

    /// @brief 构造并立即按协议创建 socket。
    /// @param[in] protocol 协议对象。
    explicit BasicSocket(const Protocol& protocol)
      : fd_{ create(protocol) }
    {}

    BasicSocket(const BasicSocket&) = delete;
    auto operator=(const BasicSocket&) -> BasicSocket& = delete;

    BasicSocket(BasicSocket&& other) noexcept
      : fd_{ std::exchange(other.fd_, INVALID_SOCKET) }
    {}

    auto operator=(BasicSocket&& other) noexcept -> BasicSocket&
    {
        if (this == &other)
            return *this;

        close();

        fd_ = std::exchange(other.fd_, INVALID_SOCKET);
        return *this;
    }

    /// @brief 析构时自动关闭 fd。
    virtual ~BasicSocket()
    {
        close();
    }

    /// @brief 检查当前 socket 是否持有有效 fd。
    /// @return 持有有效 fd 返回 true。
    [[nodiscard]]
    constexpr auto is_valid() const noexcept -> bool
    {
        return fd_ != INVALID_SOCKET;
    }

    /// @brief 打开 socket。
    /// @param[in] protocol 协议对象。
    /// @throws std::runtime_error 当 socket 已打开时抛出。
    void open(const Protocol& protocol)
    {
        if (is_valid())
            throw std::runtime_error{ "Socket is already open" };

        fd_ = create(protocol);
    }

    /// @brief 绑定本地端点。
    /// @param[in] endpoint 待绑定端点。
    void bind(const endpoint_type& endpoint)
    {
        assert(is_valid());

        auto res = ::bind(this->native_handle(), endpoint.data(), endpoint.size());
        if (res == -1)
            throw_system_error("Failed to bind socket");
    }

    /// @brief 关闭 socket。
    /// @return 成功返回空 expected；失败返回对应 error_code。
    auto close() noexcept -> std::expected<void, std::error_code>
    {
        if (is_valid()) {
            auto res = ::close(fd_);
            fd_ = INVALID_SOCKET;

            if (res == -1)
                return unexpected_system_error();
        }

        return {};
    }

    /// @brief 获取底层文件描述符。
    /// @return native socket fd。
    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return fd_;
    }

    /// @brief 设置 setsockopt/getsockopt 风格 option。
    /// @tparam Option 需满足 socket_option。
    /// @param[in] value 待设置 option。
    template<socket_option Option>
    void option(const Option& value)
    {
        auto res = ::setsockopt(fd_, Option::level, Option::name, value.data(), value.size());
        if (res == -1)
            throw_system_error("Failed to set socket option[{}]", Option::name);
    }

    /// @brief 读取 setsockopt/getsockopt 风格 option。
    /// @tparam Option 需满足 socket_option。
    /// @return 当前 option 值。
    template<socket_option Option>
    auto option() const -> Option
    {
        Option option{};
        auto size = static_cast<socklen_t>(option.size());
        auto res = ::getsockopt(fd_, Option::level, Option::name, option.data(), &size);
        if (res == -1)
            throw_system_error("Failed to get socket option[{}]", Option::name);

        if (size != option.size())
            throw std::runtime_error{ "Unexpected socket option size" };

        return option;
    }

    /// @brief 设置 fcntl flag 风格 option。
    /// @tparam Option 需满足 flag_option。
    /// @param[in] value 目标开关状态。
    template<flag_option Option>
    void option(const Option& value)
    {
        auto current_flags = ::fcntl(fd_, Option::get_cmd);
        if (current_flags == -1)
            throw_system_error("Failed to get socket flags");

        auto new_flags = value ? (current_flags | Option::bit) : (current_flags & ~Option::bit);
        if (::fcntl(fd_, Option::set_cmd, new_flags) == -1)
            throw_system_error("Failed to set socket flags");
    }

    /// @brief 读取 fcntl flag 风格 option。
    /// @tparam Option 需满足 flag_option。
    /// @return 当前 flag 开关状态。
    template<flag_option Option>
    auto option() const -> Option
    {
        auto current_flags = ::fcntl(fd_, Option::get_cmd);
        if (current_flags == -1)
            throw_system_error("Failed to get socket flags");

        if (current_flags & Option::bit)
            return Option{ true };

        return Option{ false };
    }
};

} // namespace net

#endif // BLOG_NET_SOCKET_H