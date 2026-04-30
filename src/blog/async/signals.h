#ifndef BLOG_ASYNC_SIGNALS_H
#define BLOG_ASYNC_SIGNALS_H

#include <csignal>
#include <utility>

#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "async/this_coroutine.h"
#include "common/exceptions.h"
#include "io_context.h"
#include "poll_awaiter.h"

namespace async {

/**
 * @brief Strong-typed wrapper around a POSIX signal number.
 *
 * Prevents signal numbers from being confused with plain integers at
 * call sites, and makes signal-based APIs self-documenting.
 */
class Signal {
public:
    explicit constexpr Signal(int signal)
      : signal_{ signal }
    {}

    auto operator==(const Signal&) const noexcept -> bool = default;

    constexpr operator int() const noexcept { return signal_; }

    [[nodiscard]]
    constexpr auto value() const noexcept { return signal_; }

private:
    int signal_;
};


/**
 * @brief Named constants for commonly used POSIX signals.
 *
 * Used with `SignalSet` to register signal interest without looking up
 * signal numbers manually.
 */
struct signals {
    signals() = delete;
    
    static constexpr auto interrupt = Signal{ SIGINT };
    static constexpr auto terminate = Signal{ SIGTERM };
    static constexpr auto quit = Signal{ SIGQUIT };
    static constexpr auto hangup = Signal{ SIGHUP };
};


/**
 * @brief Block a set of signals and expose them as async events via `signalfd`.
 *
 * Signals added to the set are masked from normal delivery using
 * `pthread_sigmask`. Instead, callers `co_await async_wait()` to receive
 * them through the event loop, which avoids signal-handler race conditions.
 */
class SignalSet {
public:
    /**
     * @brief Mask the specified signals and create the underlying `signalfd`.
     *
     * @tparam Signals Pack of `Signal` values.
     * @param io_context I/O context used to drive `async_wait`.
     * @param sigal      One or more signals to block and monitor.
     * @throws std::system_error If `pthread_sigmask` or `signalfd` fails.
     */
    template<typename... Signals>
        requires (std::same_as<Signals, Signal> && ...)
    SignalSet(IOContext& io_context, Signals... sigal)
      : io_context_{ io_context }
    {
        ::sigemptyset(&mask_);
        (::sigaddset(&mask_, sigal), ...);

        if (::pthread_sigmask(SIG_BLOCK, &mask_, nullptr) == -1)
            throw_system_error("Failed to block signals");

        fd_ = ::signalfd(-1, &mask_, SFD_NONBLOCK | SFD_CLOEXEC);
        if (fd_ == -1)
            throw_system_error("Failed to create signalfd");
    }

    template<typename... Signals>
        requires (std::same_as<Signals, Signal> && ...)
    SignalSet(Signals... sigal)
      : SignalSet{ this_coroutine::context(), sigal... }
    {}

    SignalSet(const SignalSet&) = delete;
    auto operator=(const SignalSet&) -> SignalSet& = delete;

    SignalSet(SignalSet&& other) noexcept
      : io_context_{ other.io_context_ }, 
        fd_{ std::exchange(other.fd_, -1) },
        mask_{ other.mask_ }
    {}

    auto operator=(SignalSet&& other) noexcept -> SignalSet& = delete;

    ~SignalSet()
    {
        if (fd_ != -1)
            ::close(fd_);
    }

    /**
     * @brief Suspend until at least one registered signal is delivered.
     *
     * Callers should drain the `signalfd` after resuming to consume the
     * pending `signalfd_siginfo` record.
     *
     * @return Awaiter that completes when the fd is readable.
     */
    auto async_wait() noexcept -> PollAwaiter
    {
        return PollAwaiter{ io_context_, fd_, POLLIN };
    }

private:
    IOContext& io_context_;
    int fd_{ -1 };
    sigset_t mask_;
};

} // namespace async

#endif // BLOG_ASYNC_SIGNALS_H