#ifndef BLOG_ASYNC_SCOPE_H
#define BLOG_ASYNC_SCOPE_H

#include <atomic>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <utility>

#include <awaitable.h>
#include <co_spawn.h>
#include <stop_requested_awaiter.h>
#include <task.h>
#include <this_coroutine.h>

namespace async {

namespace detail {

template<typename Awaitable>
auto scoped_task(Awaitable awaitable, std::shared_ptr<struct ScopeState> state) -> Task<>;

struct ScopeState {
    explicit ScopeState(IOContext& context)
      : context_{ &context }
    {}

    IOContext* context_;
    std::stop_source stop_source_;
    std::stop_source drained_;
    std::atomic_size_t pending_{ 1 }; // 1 sentinel: released by join()
};

template<typename Awaitable>
auto scoped_task(Awaitable awaitable, std::shared_ptr<ScopeState> state) -> Task<>
{
    co_await std::move(awaitable);

    if (state->pending_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        state->drained_.request_stop();
}

} // namespace detail

class Scope {
public:
    explicit Scope(IOContext& context = this_coroutine::context())
      : state_{ std::make_shared<detail::ScopeState>(context) }
    {}

    Scope(const Scope&) = delete;
    auto operator=(const Scope&) -> Scope& = delete;

    Scope(Scope&&) noexcept = default;
    auto operator=(Scope&&) noexcept -> Scope& = default;

    ~Scope()
    {
        if (state_) {
            closed_ = true;
            request_stop();
        }
    }

    template<awaitable Awaitable>
        requires std::movable<std::remove_cvref_t<Awaitable>>
    void spawn(Awaitable awaitable)
    {
        if (closed_)
            throw std::logic_error{ "Cannot spawn after join on async::Scope" };

        state_->pending_.fetch_add(1, std::memory_order_relaxed);
        co_spawn(detail::scoped_task(std::move(awaitable), state_), *state_->context_);
    }

    void request_stop() noexcept
    {
        state_->stop_source_.request_stop();
    }

    [[nodiscard]]
    auto stop_token() const noexcept -> std::stop_token
    {
        return state_->stop_source_.get_token();
    }

    auto join() -> Task<>
    {
        closed_ = true;

        // Release the sentinel count. If we transition 1 → 0 all spawned tasks
        // already finished (or none were spawned), so we are done immediately.
        if (state_->pending_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            co_return;

        co_await StopRequestedAwaiter(state_->drained_.get_token());
    }

    auto context() const noexcept -> IOContext&
    {
        return *state_->context_;
    }

private:
    std::shared_ptr<detail::ScopeState> state_;
    bool closed_{ false };
};

inline auto scope(IOContext& context = this_coroutine::context()) -> Scope
{
    return Scope{ context };
}

} // namespace async

#endif // BLOG_ASYNC_SCOPE_H