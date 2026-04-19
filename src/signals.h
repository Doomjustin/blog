#ifndef BLOG_SIGNALS_H
#define BLOG_SIGNALS_H

#include <csignal>
#include <utility>

#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "exceptions.h"
#include "poll_awaiter.h"

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


struct signals {
    signals() = delete;
    
    static constexpr auto interrupt = Signal{ SIGINT };
    static constexpr auto terminate = Signal{ SIGTERM };
    static constexpr auto quit = Signal{ SIGQUIT };
    static constexpr auto hangup = Signal{ SIGHUP };
};


template<typename Context>
class SignalSet {
public:
    template<typename... Signals>
        requires (std::same_as<Signals, Signal> && ...)
    SignalSet(Context& io_context, Signals... sigal)
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

    auto async_wait() noexcept -> PollAwaiter<Context>
    {
        return PollAwaiter<Context>{ io_context_, fd_, POLLIN };
    }

private:
    Context& io_context_;
    int fd_{ -1 };
    sigset_t mask_;
};

#endif // BLOG_SIGNALS_H