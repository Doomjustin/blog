#ifndef BLOG_SOCKET_H
#define BLOG_SOCKET_H

#include <concepts>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "exceptions.h"
#include "linger.h"
#include "option.h"

template <typename T>
concept has_domain = requires 
{ 
    { T::domain } -> std::convertible_to<int>;
};

template <typename T>
concept has_type = requires 
{ 
    { T::type } -> std::convertible_to<int>;
};

template <typename T>
concept has_protocol = requires
{
    { T::protocol } -> std::convertible_to<int>;
};

template <typename T>
concept socket_protocol = has_domain<T> && has_type<T> && has_protocol<T>;


template<typename Context, socket_protocol Protocol>
class BaseSocket {
public:
    static constexpr int domain = Protocol::domain;
    static constexpr int type = Protocol::type;
    static constexpr int protocol = Protocol::protocol;

    using reuse_address = BooleanOption<SOL_SOCKET, SO_REUSEADDR>;
    
#ifdef SO_REUSEPORT
    using reuse_port = BooleanOption<SOL_SOCKET, SO_REUSEPORT>;
#endif

    using error = BooleanOption<SOL_SOCKET, SO_ERROR>;

    using receive_buffer_size = ValueOption<SOL_SOCKET, SO_RCVBUF>;
    
    using send_buffer_size = ValueOption<SOL_SOCKET, SO_SNDBUF>;

    using linger = LingerOption;
    
    using non_blocking = FlagOption<F_GETFL, F_SETFL, O_NONBLOCK>;
    
    using close_on_exec = FlagOption<F_GETFD, F_SETFD, FD_CLOEXEC>;

    BaseSocket(Context& context)
      : fd_{ create(domain, type, protocol) }, 
        context_{ &context }
    {}

    BaseSocket(const BaseSocket&) = delete;
    auto operator=(const BaseSocket&) -> BaseSocket& = delete;

    BaseSocket(BaseSocket&& other) noexcept
      : fd_{ std::exchange(other.fd_, INVALID_SOCKET) },
        context_{ std::exchange(other.context_, nullptr) }
    {}

    auto operator=(BaseSocket&& other) noexcept -> BaseSocket&
    {
        if (this == &other) return *this;

        close();

        fd_ = std::exchange(other.fd_, INVALID_SOCKET);
        context_ = std::exchange(other.context_, nullptr);
        return *this;
    }

    virtual ~BaseSocket()
    {
        close();
    }

    [[nodiscard]]
    constexpr auto is_valid() const noexcept -> bool
    {
        return fd_ != INVALID_SOCKET;
    }

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

    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return fd_;
    }

    template<socket_option Option>
    void option(const Option& value)
    {
        auto res = ::setsockopt(fd_, Option::level, Option::name, value.data(), value.size());
        if (res == -1)
            throw_system_error("Failed to set socket option[{}]", Option::name);
    }

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

    [[nodiscard]]
    auto context() noexcept -> Context&
    {
        return *context_;
    }

    [[nodiscard]]
    auto context() const noexcept -> const Context&
    {
        return *context_;
    }

protected:
    explicit BaseSocket(Context& context, int fd)
      : fd_{ fd }, 
        context_{ &context }
    {}
    
private:
    static constexpr int INVALID_SOCKET = -1;

    int fd_ = INVALID_SOCKET;
    Context* context_;

    static auto create(int domain, int type, int protocol) -> int
    {
        auto res = ::socket(domain, type, protocol);
        if (res == -1)
            throw_system_error("Failed to create socket");

        return res;
    }
};

#endif // BLOG_SOCKET_H