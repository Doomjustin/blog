#include <cassert>
#include <cstdlib>

#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/poll.h>

#include <liburing.h>
#include <spdlog/spdlog.h>

#include "co_spawn.h"
#include "exceptions.h"
#include "io_context.h"
#include "operation.h"
#include "option.h"
#include "readsome_awaiter.h"
#include "signals.h"
#include "task.h"
#include "timeout.h"
#include "writesome_awaiter.h"

template<typename Socket>
class AcceptAwaiter: public Operation {
public:
    using context_type = typename Socket::context_type;
    using socket_type = Socket;
    using resume_type = socket_type;

    AcceptAwaiter(context_type& context, int fd)
      : context_{ context }, fd_{ fd }
    {} 

    [[nodiscard]] 
    constexpr auto await_ready() const noexcept -> bool
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

        return resume_type{ context_, result_fd_ };
    }

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_accept(sqe, fd_, nullptr, nullptr, 0);
    }

    void set_result(int result, std::uint32_t flags) noexcept
    {
        if (result >= 0)
            result_fd_ = result;
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

    auto context() noexcept -> context_type& { return context_; }

private:
    context_type& context_;
    int fd_;
    socklen_t addrlen_;

    std::coroutine_handle<> handle_{ nullptr };
    int result_fd_{ -1 };
    int error_code_{ 0 };
};


template<typename Context>
class Socket {
public:
    using context_type = Context;

    using reuse_address = BooleanOption<SOL_SOCKET, SO_REUSEADDR>;
    
#ifdef SO_REUSEPORT
    using reuse_port = BooleanOption<SOL_SOCKET, SO_REUSEPORT>;
#endif

    using error = BooleanOption<SOL_SOCKET, SO_ERROR>;

    using receive_buffer_size = ValueOption<SOL_SOCKET, SO_RCVBUF>;
    
    using send_buffer_size = ValueOption<SOL_SOCKET, SO_SNDBUF>;

    using non_blocking = FlagOption<F_GETFL, F_SETFL, O_NONBLOCK>;
    
    using close_on_exec = FlagOption<F_GETFD, F_SETFD, FD_CLOEXEC>;

    Socket(Context& context, int domain, int type, int protocol)
      : context_{ context }
    {
        fd_ = socket(domain, type, protocol);
        if (fd_ == -1)
            throw_system_error("Failed to create socket");
    }

    Socket(Context& context, int fd)
      : context_{ context }, 
        fd_(fd) 
    {}

    Socket(const Socket&) = delete;
    auto operator=(const Socket&) -> Socket& = delete;

    Socket(Socket&& other) noexcept
      : context_{ other.context_ }, 
        fd_{ std::exchange(other.fd_, -1) }
    {}

    auto operator=(Socket&& other) noexcept -> Socket&
    {
        if (this != &other) {
            close();

            context_ = other.context_;
            fd_ = std::exchange(other.fd_, -1);
        }

        return *this;
    }

    ~Socket()
    {
        close();
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

    void close()
    {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    [[nodiscard]]
    constexpr auto native_handle() const -> int
    {
        return fd_;
    }

    void bind(const sockaddr* addr, socklen_t addrlen)
    {
        if (::bind(fd_, addr, addrlen) == -1)
            throw_system_error("Failed to bind socket");
    }

    void listen(int backlog)
    {
        if (::listen(fd_, backlog) == -1)
            throw_system_error("Failed to listen on socket");
    }

    auto accept(sockaddr* addr, socklen_t* addrlen) -> Socket<Context>
    {
        int client_fd = ::accept(fd_, addr, addrlen);
        if (client_fd == -1)
            throw_system_error("Failed to accept connection");

        return { context_, client_fd };
    }

    auto async_readsome(std::span<std::byte> buffer) -> ReadSomeAwaiter<Context>
    {
        return ReadSomeAwaiter<Context>{ context_, fd_, buffer };
    }

    auto async_writesome(std::span<const std::byte> buffer) -> WriteSomeAwaiter<Context>
    {
        return WriteSomeAwaiter<Context>{ context_, fd_, buffer };
    }

    auto async_accept() -> AcceptAwaiter<Socket<Context>>
    {
        return AcceptAwaiter<Socket<Context>>{ context_, fd_ };
    }

private:
    Context& context_;
    int fd_;
};

auto shutdown_monitor(IOContext& context) -> Task<void>
{
    using namespace std::chrono_literals;

    SignalSet sets{ context, signals::interrupt, signals::terminate };

    co_await sets.async_wait();

    spdlog::info("Received shutdown signal, stopping IOContext...");
    context.stop();
}

auto session(Socket<IOContext> client) -> Task<void>
{
    std::array<std::byte, 1024> buffer{};

    while (true)
    {
        using namespace std::chrono_literals;
        auto read_result = co_await timeout(client.async_readsome(buffer), 5s);
        // auto read_result = co_await client.async_readsome(buffer);
        if (!read_result) {
            if (read_result.error() == std::errc::timed_out) {
                spdlog::debug("Read timed out on client {}", client.native_handle());
                continue;
            }

            spdlog::warn("Failed to read from client {}: {}", client.native_handle(), read_result.error().message());
            co_return;
        }

        auto bytes_read = *read_result;
        if (bytes_read == 0) {
            spdlog::info("Client {} disconnected", client.native_handle());
            co_return;
        }

        spdlog::info("Read {} bytes from client {}", bytes_read, client.native_handle());
        std::string data{ reinterpret_cast<const char*>(buffer.data()), bytes_read };
        spdlog::warn("Data from client {}: {}", client.native_handle(), data);

        auto write_buffer = std::span{ buffer }.first(bytes_read);

        auto write_result = co_await timeout(client.async_writesome(write_buffer), 5s);
        // auto write_result = co_await client.async_writesome(write_buffer);
        if (!write_result) {
            if (write_result.error() == std::errc::timed_out) {
                spdlog::debug("Write timed out on client {}", client.native_handle());
                continue;
            }

            spdlog::warn("Failed to write to client {}: {}", client.native_handle(), write_result.error().message());
            co_return;
        }

        if (*write_result != bytes_read)
            spdlog::warn("Partial write on client {}: {} / {} bytes", client.native_handle(), *write_result, bytes_read);
    }
}

auto server(IOContext& context) -> Task<void>
{
    Socket acceptor{ context, AF_INET, SOCK_STREAM, 0 };
    acceptor.option(Socket<IOContext>::reuse_address{ true });

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = ::htons(12345);
    acceptor.bind(reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    acceptor.listen(1024);

    while (true) {
        auto client = co_await acceptor.async_accept();
        if (!client) {
            spdlog::warn("Failed to accept client connection: {}", client.error().message());
            continue;
        }

        co_spawn(context, session(std::move(*client)));
    }
}

int main(int argc, char* argv[])
{
    IOContext context{};

    co_spawn(context, server(context));
    co_spawn(context, shutdown_monitor(context));

    context.run();

    spdlog::info("IOContext stopped, exiting...");

    return EXIT_SUCCESS;
}