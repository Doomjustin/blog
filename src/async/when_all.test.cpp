#include <when_all.h>

#include <cerrno>
#include <coroutine>
#include <expected>
#include <memory>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include <when_any.h>

namespace {

struct DummyContext {};

struct FakeState {
    bool cancel_called{ false };
    bool suspended{ false };
    bool arm_ok{ true };
    async::CancelableOperation* self{ nullptr };
};

class FakeCancelable final: public async::CancelableOperation {
public:
    using resume_type = int;

    explicit FakeCancelable(std::shared_ptr<FakeState> state, bool arm_ok = true)
      : state_{ std::move(state) }
    {
        state_->arm_ok = arm_ok;
    }

    [[nodiscard]]
    constexpr auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
    {
        handle_ = handle;
        state_->self = this;
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

TEST_CASE("when_all: collects results after all completions", "[async][when_all]")
{
    auto first = std::make_shared<FakeState>();
    auto second = std::make_shared<FakeState>();

    auto awaiter = async::when_all(FakeCancelable{ first }, FakeCancelable{ second });

    REQUIRE(awaiter.await_suspend(std::noop_coroutine()));
    REQUIRE(first->self != nullptr);
    REQUIRE(second->self != nullptr);

    first->self->complete(11, 0);
    second->self->complete(22, 0);

    auto [r1, r2] = awaiter.await_resume();
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());
    REQUIRE(*r1 == 11);
    REQUIRE(*r2 == 22);
}

TEST_CASE("when_all: partial arming failure does not deadlock", "[async][when_all]")
{
    auto failed = std::make_shared<FakeState>();
    auto armed = std::make_shared<FakeState>();

    auto awaiter = async::when_all(FakeCancelable{ failed, false }, FakeCancelable{ armed, true });

    REQUIRE(awaiter.await_suspend(std::noop_coroutine()));
    REQUIRE(armed->self != nullptr);

    armed->self->complete(7, 0);

    auto [r_failed, r_armed] = awaiter.await_resume();
    REQUIRE_FALSE(r_failed.has_value());
    REQUIRE(r_failed.error() == std::error_code(EAGAIN, std::generic_category()));
    REQUIRE(r_armed.has_value());
    REQUIRE(*r_armed == 7);
}

TEST_CASE("when_all: all arming failures skip suspension", "[async][when_all]")
{
    auto first = std::make_shared<FakeState>();
    auto second = std::make_shared<FakeState>();

    auto awaiter = async::when_all(FakeCancelable{ first, false }, FakeCancelable{ second, false });

    REQUIRE_FALSE(awaiter.await_suspend(std::noop_coroutine()));

    auto [r1, r2] = awaiter.await_resume();
    REQUIRE_FALSE(r1.has_value());
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r1.error() == std::error_code(EAGAIN, std::generic_category()));
    REQUIRE(r2.error() == std::error_code(EAGAIN, std::generic_category()));
}

TEST_CASE("when_all: cancel propagates into nested when_any leaves", "[async][when_all][cancel]")
{
    auto nested1 = std::make_shared<FakeState>();
    auto nested2 = std::make_shared<FakeState>();
    auto direct = std::make_shared<FakeState>();

    auto nested_any = async::when_any(FakeCancelable{ nested1 }, FakeCancelable{ nested2 });
    auto awaiter = async::when_all(std::move(nested_any), FakeCancelable{ direct });

    REQUIRE(awaiter.await_suspend(std::noop_coroutine()));

    awaiter.cancel();

    REQUIRE(nested1->cancel_called);
    REQUIRE(nested2->cancel_called);
    REQUIRE(direct->cancel_called);
}
