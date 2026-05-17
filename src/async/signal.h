#ifndef BLOG_ASYNC_SIGNAL_H
#define BLOG_ASYNC_SIGNAL_H

#include <csignal>

#include <liburing.h>
#include <sys/signalfd.h>

#include "io_awaiter.h"

namespace async {

/// @brief 强类型的 POSIX signal 封装。
///
/// 用于避免裸 `int` 在调用处混淆，并提升 API 可读性。
class Signal {
public:
    /// @brief 构造一个 signal 包装值。
    /// @param[in] signal POSIX signal number。
    explicit constexpr Signal(int signal)
      : signal_{ signal }
    {}

    auto operator==(const Signal&) const noexcept -> bool = default;

    constexpr operator int() const noexcept
    {
        return signal_;
    }

    [[nodiscard]]
    constexpr auto value() const noexcept
    {
        return signal_;
    }

private:
    int signal_;
};

struct signals {
    signals() = delete;

    /// @brief 在当前线程屏蔽指定信号集合。
    /// @tparam Sig `Signal` 类型参数包。
    /// @param[in] signal 要屏蔽的信号。
    /// @throws std::system_error 当 `pthread_sigmask` 失败。
    template<typename... Sig>
        requires(std::same_as<Sig, Signal> && ...)
    static void block(Sig... signal)
    {
        sigset_t mask;
        ::sigemptyset(&mask);
        (::sigaddset(&mask, signal), ...);

        if (auto err = ::pthread_sigmask(SIG_BLOCK, &mask, nullptr); err != 0)
            throw_system_error(err, "Failed to block signals");
    }

    /// @brief 常见信号预定义集合（可按需组合传给 `block` / `SignalSet`）。
    static constexpr auto interrupt = Signal{ SIGINT };
    static constexpr auto terminate = Signal{ SIGTERM };
    static constexpr auto quit = Signal{ SIGQUIT };
    static constexpr auto hangup = Signal{ SIGHUP };
    static constexpr auto child = Signal{ SIGCHLD };
    static constexpr auto user1 = Signal{ SIGUSR1 };
    static constexpr auto user2 = Signal{ SIGUSR2 };
    static constexpr auto alarm = Signal{ SIGALRM };
    static constexpr auto broken_pipe = Signal{ SIGPIPE };
    static constexpr auto continue_ = Signal{ SIGCONT };
    static constexpr auto terminal_stop = Signal{ SIGTSTP };
    static constexpr auto window_change = Signal{ SIGWINCH };
};

/// @brief 通过 `signalfd` 等待信号到达的 Awaiter。
///
/// 该 awaiter 基于 `io_uring_prep_read` 读取 `signalfd_siginfo`：
/// - completion 成功后可通过 `result()` 读取 `ssi_signo`。
/// - 取消/错误语义由 `SingleShotAwaiter` 路径统一处理。
class SignalAwaiter : public IOAwaiter<SignalAwaiter, int> {
private:
    int fd_;
    ::signalfd_siginfo info_{};

public:
    /// @brief 构造 SignalAwaiter。
    /// @param[in] fd 已创建的 signalfd 描述符。
    SignalAwaiter(int fd) noexcept
      : fd_{ fd }
    {}

    /// @brief 为本次等待准备 read SQE。
    /// @param[in,out] sqe 待填充的 SQE。
    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_read(sqe, fd_, &info_, sizeof(info_), 0);
    }

    /// @brief 返回最近一次读取到的 signal number。
    /// @return `signalfd_siginfo::ssi_signo`。
    auto value() const noexcept -> int
    {
        // 错误值由 IOAwaiter 基类处理，这里仅返回成功读取的信号即可。
        return info_.ssi_signo;
    }
};

/// @brief `signalfd` 封装，提供可 `co_await` 的信号等待接口。
///
/// 使用约束：
/// - 构造时会在当前线程调用 `pthread_sigmask(SIG_BLOCK, ...)`。
/// - 仅屏蔽当前线程；若需进程级一致行为，应在创建工作线程前完成屏蔽。
/// - `async_wait()` 每次返回一个单次等待的 awaiter。
class SignalSet {
private:
    int fd_{ -1 };
    sigset_t mask_;

public:
    /// @brief 构造并创建 signalfd。
    /// @tparam Signals `Signal` 类型参数包。
    /// @param[in] sigal 待监听并在当前线程屏蔽的信号集合。
    /// @throws std::system_error 当 `pthread_sigmask` 或 `signalfd` 失败。
    template<typename... Signals>
        requires(std::same_as<Signals, Signal> && ...)
    SignalSet(Signals... sigal)
    {
        ::sigemptyset(&mask_);
        (::sigaddset(&mask_, sigal), ...);

        if (auto err = ::pthread_sigmask(SIG_BLOCK, &mask_, nullptr); err != 0)
            throw_system_error(err, "Failed to block signals");

        fd_ = ::signalfd(-1, &mask_, SFD_NONBLOCK | SFD_CLOEXEC);
        if (fd_ == -1)
            throw_system_error("Failed to create signalfd");
    }

    SignalSet(const SignalSet&) = delete;
    auto operator=(const SignalSet&) -> SignalSet& = delete;

    SignalSet(SignalSet&& other) noexcept
      : fd_{ std::exchange(other.fd_, -1) }
      , mask_{ other.mask_ }
    {}

    auto operator=(SignalSet&& other) noexcept -> SignalSet& = delete;

    ~SignalSet()
    {
        if (fd_ != -1)
            ::close(fd_);
    }

    /// @brief 异步等待下一次信号到达。
    /// @return `SignalAwaiter`，completion 后其 `value()` 为 signal number。
    auto async_wait() const noexcept -> SignalAwaiter
    {
        return SignalAwaiter{ fd_ };
    }
};

auto format_as(const Signal& signal) -> std::string_view
{
    switch (signal) {
    case signals::interrupt:
        return "interrupt";
    case signals::terminate:
        return "terminate";
    case signals::quit:
        return "quit";
    case signals::hangup:
        return "hangup";
    case signals::child:
        return "child";
    case signals::user1:
        return "user1";
    case signals::user2:
        return "user2";
    case signals::alarm:
        return "alarm";
    case signals::broken_pipe:
        return "broken_pipe";
    case signals::continue_:
        return "continue";
    case signals::terminal_stop:
        return "terminal_stop";
    case signals::window_change:
        return "window_change";
    default:
        return "unknown";
    }
}

} // namespace async

#endif // BLOG_ASYNC_SIGNAL_H