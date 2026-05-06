#include <scope.h>

#include <atomic>
#include <chrono>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include <run.h>
#include <sleep_for.h>
#include <stop_requested_awaiter.h>

using namespace std::chrono_literals;

namespace {

auto delayed_increment(std::atomic_int& counter, std::chrono::milliseconds delay) -> async::Task<>
{
    co_await async::sleep_for(delay);
    counter.fetch_add(1, std::memory_order_relaxed);
}

auto wait_for_scope_stop(std::atomic_bool& stopped, std::stop_token token) -> async::Task<>
{
    co_await async::StopRequestedAwaiter(token);
    stopped.store(true, std::memory_order_relaxed);
}

} // namespace

TEST_CASE("scope: join waits for all spawned tasks", "[async][scope]")
{
    std::atomic_int completed{ 0 };

    async::run([&]() -> async::Task<> {
        auto group = async::scope();

        group.spawn(delayed_increment(completed, 5ms));
        group.spawn(delayed_increment(completed, 10ms));

        co_await group.join();
    });

    REQUIRE(completed.load(std::memory_order_relaxed) == 2);
}

TEST_CASE("scope: request_stop releases stop-aware children before join returns", "[async][scope]")
{
    std::atomic_bool stopped{ false };

    async::run([&]() -> async::Task<> {
        auto group = async::scope();

        group.spawn(wait_for_scope_stop(stopped, group.stop_token()));
        co_await async::sleep_for(1ms);

        group.request_stop();
        co_await group.join();
    });

    REQUIRE(stopped.load(std::memory_order_relaxed));
}

TEST_CASE("scope: destruction requests stop for spawned children", "[async][scope]")
{
    std::atomic_bool stopped{ false };

    async::run([&]() -> async::Task<> {
        {
            auto group = async::scope();
            group.spawn(wait_for_scope_stop(stopped, group.stop_token()));
        }

        co_return;
    });

    REQUIRE(stopped.load(std::memory_order_relaxed));
}

TEST_CASE("scope: spawn after join is rejected", "[async][scope]")
{
    async::Scope group;

    auto join_task = group.join();
    REQUIRE_FALSE(join_task.done());

    join_task.handle().resume();
    REQUIRE(join_task.done());

    REQUIRE_THROWS_AS(group.spawn(async::sleep_for(1ms)), std::logic_error);
}