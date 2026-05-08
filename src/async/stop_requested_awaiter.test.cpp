#include <async/stop_requested_awaiter.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <stop_token>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <async/io_context.h>

namespace {

struct ResumeProbe {
    struct promise_type {
        auto get_return_object() -> ResumeProbe
        {
            return ResumeProbe{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        auto initial_suspend() noexcept -> std::suspend_always { return {}; }
        auto final_suspend() noexcept -> std::suspend_always { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };

    explicit ResumeProbe(std::coroutine_handle<promise_type> handle)
      : handle_{ handle }
    {}

    ResumeProbe(ResumeProbe&& other) noexcept
      : handle_{ std::exchange(other.handle_, {}) }
    {}

    ~ResumeProbe()
    {
        if (handle_)
            handle_.destroy();
    }

    std::coroutine_handle<promise_type> handle_;
};

auto make_probe(std::condition_variable& cv, std::atomic<int>& resumes) -> ResumeProbe
{
    resumes.fetch_add(1, std::memory_order_relaxed);
    cv.notify_all();
    co_return;
}

} // namespace

TEST_CASE("stop_requested_awaiter: pre-stopped token is ready", "[async][stop_requested]")
{
    async::IOContext context;
    std::stop_source source;
    source.request_stop();

    auto awaiter = async::StopRequestedAwaiter(context, source.get_token());
    REQUIRE(awaiter.await_ready());
}

TEST_CASE("stop_requested_awaiter: synchronous stop callback posts a single resume", "[async][stop_requested]")
{
    async::IOContext context;
    context.add_work();

    std::jthread io_thread([&context] {
        context.run();
    });

    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> resumes{ 0 };

    std::stop_source source;
    auto awaiter = async::StopRequestedAwaiter(context, source.get_token());
    REQUIRE_FALSE(awaiter.await_ready());

    auto probe = make_probe(cv, resumes);

    source.request_stop();
    REQUIRE(awaiter.await_suspend(probe.handle_));

    auto resumed = [&] {
        std::unique_lock lock{ mutex };
        return cv.wait_for(lock, std::chrono::seconds(1), [&] { return resumes.load(std::memory_order_relaxed) == 1; });
    }();

    context.stop();
    context.drop_work();
    io_thread.join();

    REQUIRE(resumed);
    REQUIRE(resumes.load(std::memory_order_relaxed) == 1);

    awaiter.await_resume();
}