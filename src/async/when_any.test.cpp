#include <when_any.h>

#include <cerrno>
#include <coroutine>
#include <expected>
#include <memory>
#include <system_error>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <when_all.h>

namespace {

struct DummyContext {};

struct FakeState {
    bool cancel_called{ false };
    bool suspended{ false };
    bool arm_ok{ true };
    bool ready{ false };
    int suspend_calls{ 0 };
    int cancel_calls{ 0 };
    int ready_result{ 0 };
    async::CancelableOperation* self{ nullptr };
};

class FakeCancelable final: public async::CancelableOperation {
public:
    using resume_type = int;

    explicit FakeCancelable(std::shared_ptr<FakeState> state, bool arm_ok = true, bool ready = false, int ready_result = 0)
      : state_{ std::move(state) }
    {
        state_->arm_ok = arm_ok;
        state_->ready = ready;
        state_->ready_result = ready_result;
        result_ = ready_result;
    }

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return state_->ready;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        state_->self = this;
        ++state_->suspend_calls;
        state_->suspended = true;

        if (!state_->arm_ok) {
            error_code_ = EAGAIN;
            return false;
        }

        return true;
    }

    auto await_resume() noexcept -> std::expected<resume_type, std::error_code>
    {
        if (error_code_ != 0)
            return std::unexpected(std::error_code(error_code_, std::generic_category()));

        return result_;
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        (void)flags;

        if (result < 0)
            error_code_ = -result;
        else
            result_ = result;

        this->resume(handle_, result, flags);
    }

    void cancel() noexcept override
    {
        ++state_->cancel_calls;
        state_->cancel_called = true;
    }

    auto context() noexcept -> DummyContext&
    {
        return context_;
    }

private:
    DummyContext context_{};
    std::shared_ptr<FakeState> state_;
    std::coroutine_handle<> handle_;
    int result_{ 0 };
    int error_code_{ 0 };
};

} // namespace

TEST_CASE("when_any: winner completion cancels leaf losers", "[async][when_any][cancel]")
{
    auto winner_state = std::make_shared<FakeState>();
    auto loser_state = std::make_shared<FakeState>();

    auto awaiter = async::when_any(FakeCancelable{ winner_state }, FakeCancelable{ loser_state });

    REQUIRE(awaiter.await_suspend(std::noop_coroutine()));
    REQUIRE(winner_state->self != nullptr);
    REQUIRE(loser_state->self != nullptr);

    winner_state->self->complete(1, 0);
    REQUIRE(loser_state->cancel_called);

    loser_state->self->complete(-ECANCELED, 0);

    auto result = awaiter.await_resume();
    REQUIRE(result.has_value());
}

TEST_CASE("when_any: cancel propagates into nested when_all leaves", "[async][when_any][cancel]")
{
    auto winner_state = std::make_shared<FakeState>();
    auto leaf1_state = std::make_shared<FakeState>();
    auto leaf2_state = std::make_shared<FakeState>();

    auto nested_loser = async::when_all(FakeCancelable{ leaf1_state }, FakeCancelable{ leaf2_state });
    auto awaiter = async::when_any(FakeCancelable{ winner_state }, std::move(nested_loser));

    REQUIRE(awaiter.await_suspend(std::noop_coroutine()));
    REQUIRE(winner_state->self != nullptr);

    winner_state->self->complete(1, 0);

    REQUIRE(leaf1_state->cancel_called);
    REQUIRE(leaf2_state->cancel_called);

    leaf1_state->self->complete(-ECANCELED, 0);
    leaf2_state->self->complete(-ECANCELED, 0);

    auto result = awaiter.await_resume();
    REQUIRE(result.index() == 0);
    REQUIRE(std::get<0>(result).has_value());
}

TEST_CASE("when_any: arming failure picks winner and cancels armed losers", "[async][when_any][cancel]")
{
    auto failed_state = std::make_shared<FakeState>();
    auto armed_state = std::make_shared<FakeState>();

    auto awaiter = async::when_any(FakeCancelable{ failed_state, false }, FakeCancelable{ armed_state, true });

    REQUIRE(awaiter.await_suspend(std::noop_coroutine()));
    REQUIRE(armed_state->cancel_called);

    armed_state->self->complete(-ECANCELED, 0);

    auto result = awaiter.await_resume();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == std::error_code(EAGAIN, std::generic_category()));
}

TEST_CASE("when_any: ready winner is not suspended and only armed losers are canceled", "[async][when_any][cancel]")
{
    auto ready_state = std::make_shared<FakeState>();
    auto loser_state = std::make_shared<FakeState>();

    auto awaiter = async::when_any(FakeCancelable{ ready_state, true, true, 9 }, FakeCancelable{ loser_state, true });

    REQUIRE(awaiter.await_suspend(std::noop_coroutine()));
    REQUIRE(ready_state->suspend_calls == 0);
    REQUIRE(ready_state->cancel_calls == 0);
    REQUIRE(loser_state->cancel_called);
    REQUIRE(loser_state->self != nullptr);

    loser_state->self->complete(-ECANCELED, 0);

    auto result = awaiter.await_resume();
    REQUIRE(result.has_value());
    REQUIRE(*result == 9);
}
