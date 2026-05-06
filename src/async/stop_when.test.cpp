#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <system_error>
#include <thread>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include <io_context.h>
#include <stop_then.h>

namespace {

struct StopThenState {
    std::mutex mutex;
    std::condition_variable cv;
    bool cancel_called{ false };
};

class FakeSingleShotOp final: public async::CancelableOperation {
public:
    using is_single_shot = std::true_type;
    using resume_type = void;

    FakeSingleShotOp(async::IOContext& context, std::shared_ptr<StopThenState> state)
      : context_{ &context }
      , state_{ std::move(state) }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        return true;
    }

    auto await_resume() noexcept -> std::expected<void, std::error_code>
    {
        return {};
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        this->resume(handle_, result, flags);
    }

    void cancel() noexcept override
    {
        {
            std::lock_guard lock{ state_->mutex };
            state_->cancel_called = true;
        }
        state_->cv.notify_all();
    }

    auto context() noexcept -> async::IOContext&
    {
        return *context_;
    }

private:
    async::IOContext* context_;
    std::shared_ptr<StopThenState> state_;
    std::coroutine_handle<> handle_;
};

class FakeResumeSingleShotOp final: public async::CancelableOperation {
public:
    using is_single_shot = std::true_type;
    using resume_type = void;

    FakeResumeSingleShotOp(async::IOContext& context, int await_resume_error)
      : context_{ &context }
      , await_resume_error_{ await_resume_error }
    {}

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        return true;
    }

    auto await_resume() noexcept -> std::expected<void, std::error_code>
    {
        if (await_resume_error_ == ETIME || await_resume_error_ == 0)
            return {};

        return unexpected_system_error(await_resume_error_);
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        this->resume(handle_, result, flags);
    }

    void cancel() noexcept override {}

    auto context() noexcept -> async::IOContext&
    {
        return *context_;
    }

private:
    async::IOContext* context_;
    int await_resume_error_{ 0 };
    std::coroutine_handle<> handle_;
};

} // namespace

TEST_CASE("stop_then: pre-stopped token returns operation_canceled", "[async][stop_then]")
{
    async::IOContext context;
    std::stop_source source;
    source.request_stop();

    auto wrapper = async::stop_then(FakeSingleShotOp{ context, std::make_shared<StopThenState>() }, source.get_token());

    REQUIRE(wrapper.await_ready());

    auto result = wrapper.await_resume();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == std::make_error_code(std::errc::operation_canceled));
}

TEST_CASE("stop_then: stop request triggers inner cancel via post", "[async][stop_then]")
{
    async::IOContext context;
    context.add_work();

    std::jthread io_thread([&context] {
        context.run();
    });

    auto state = std::make_shared<StopThenState>();
    std::stop_source source;

    auto wrapper = async::stop_then(FakeSingleShotOp{ context, state }, source.get_token());

    REQUIRE_FALSE(wrapper.await_ready());
    wrapper.await_suspend(std::noop_coroutine());

    source.request_stop();

    auto canceled = [&] {
        std::unique_lock lock{ state->mutex };
        return state->cv.wait_for(lock, std::chrono::seconds(1), [&] { return state->cancel_called; });
    }();

    context.stop();
    context.drop_work();
    io_thread.join();

    REQUIRE(canceled);
}

TEST_CASE("stop_then: ECANCELED completion maps to operation_canceled", "[async][stop_then]")
{
    async::IOContext context;
    std::stop_source source;

    auto wrapper = async::stop_then(FakeResumeSingleShotOp{ context, 0 }, source.get_token());

    REQUIRE_FALSE(wrapper.await_ready());
    wrapper.await_suspend(std::noop_coroutine());
    wrapper.complete(-ECANCELED, 0);

    auto result = wrapper.await_resume();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == std::make_error_code(std::errc::operation_canceled));
}

TEST_CASE("stop_then: non-cancel errors are forwarded from inner await_resume", "[async][stop_then]")
{
    async::IOContext context;
    std::stop_source source;

    auto wrapper = async::stop_then(FakeResumeSingleShotOp{ context, EIO }, source.get_token());

    REQUIRE_FALSE(wrapper.await_ready());
    wrapper.await_suspend(std::noop_coroutine());
    wrapper.complete(0, 0);

    auto result = wrapper.await_resume();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().value() == EIO);
}

TEST_CASE("stop_then: inner ETIME-like success is preserved", "[async][stop_then]")
{
    async::IOContext context;
    std::stop_source source;

    auto wrapper = async::stop_then(FakeResumeSingleShotOp{ context, ETIME }, source.get_token());

    REQUIRE_FALSE(wrapper.await_ready());
    wrapper.await_suspend(std::noop_coroutine());
    wrapper.complete(0, 0);

    auto result = wrapper.await_resume();
    REQUIRE(result.has_value());
}
