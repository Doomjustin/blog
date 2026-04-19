#include <cassert>
#include <cstdlib>

#include <sys/eventfd.h>
#include <sys/poll.h>

#include <liburing.h>
#include <spdlog/spdlog.h>

#include "co_spawn.h"
#include "operation.h"
#include "signals.h"
#include "sleep_for.h"
#include "task.h"

// 只支持 core per thread 模型，所以io_context本身不需要考虑线程安全问题
class IOContext {
public:
    explicit IOContext(unsigned entries = 1024)
    {
        if (auto res = ::io_uring_queue_init(entries, &ring_, 0); res < 0)
            throw_system_error(-res, "io_uring_queue_init");            

        wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeup_fd_ == -1)
            throw_system_error("Failed to create eventfd for stopping IOContext");

        arm_wakeup();
    }

    IOContext(const IOContext&) = delete;
    auto operator=(const IOContext&) -> IOContext& = delete;

    // 为了简化实现，我们不支持移动
    IOContext(IOContext&& other) noexcept = delete;
    auto operator=(IOContext&&) -> IOContext& = delete;

    ~IOContext()
    {
        ::io_uring_queue_exit(&ring_);
        ::close(wakeup_fd_);
    }

    void run()
    {
        ::io_uring_cqe* cqe{ nullptr };

        while (!should_stop_.load(std::memory_order_relaxed) && outstanding_works_ > 0)
        {
            auto res = ::io_uring_submit_and_wait(&ring_, 1);
            if (res < 0) {
                if (res == -EINTR)
                    continue;

                throw_system_error("io_uring_submit_and_wait");
            }
                
            unsigned head;
            unsigned count{ 0 };
            unsigned workdone{ 0 };

            io_uring_for_each_cqe(&ring_, head, cqe) {
                ++count;

                if (io_uring_cqe_get_data64(cqe) == WAKEUP_MARKER) {
                    resume_wakeup();
                    arm_wakeup();
                    continue;
                }

                if (io_uring_cqe_get_data64(cqe) != 0) {
                    auto* op = static_cast<Operation*>(io_uring_cqe_get_data(cqe));
                    op->complete(cqe->res, cqe->flags);

                    ++workdone;
                }
            }

            if (count > 0)
                ::io_uring_cq_advance(&ring_, count);

            if (workdone > 0)
                outstanding_works_ -= workdone;
        }
    }

    [[nodiscard]]
    auto sqe() -> ::io_uring_sqe*
    {
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe)
            throw_system_error("io_uring_get_sqe");

        add_work();
        return sqe;
    }

    void stop()
    {
        should_stop_.store(true, std::memory_order_relaxed);
        wakeup();
    }

    auto ring() noexcept -> ::io_uring*
    {
        return &ring_;
    }

    auto ring() const noexcept -> const ::io_uring*
    {
        return &ring_;
    }

    void add_work() noexcept
    {
        ++outstanding_works_;
    }

    void drop_work() noexcept
    {
        assert(outstanding_works_ > 0);
        --outstanding_works_;
    }
    
private:
    static constexpr auto WAKEUP_MARKER = std::numeric_limits<std::uintptr_t>::max();

    ::io_uring ring_{};
    int wakeup_fd_{ -1 };

    // 只用来追踪io_context之外的操作，并不需要用户主动来使用相关的接口
    std::size_t outstanding_works_{ 0 };
    // stop会被跨线程调用，所以需要使用原子变量来保证线程安全
    std::atomic<bool> should_stop_{ false };

    void arm_wakeup() noexcept
    {
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe) return;

        ::io_uring_prep_poll_add(sqe, wakeup_fd_, POLLIN);
        ::io_uring_sqe_set_data64(sqe, WAKEUP_MARKER);
    }   
    
    void wakeup()
    {
        std::uint64_t val = 1;
        ::write(wakeup_fd_, &val, sizeof(val));
    }

    void resume_wakeup()
    {
        uint64_t val;
        ::read(wakeup_fd_, &val, sizeof(val));
    }
};

auto shutdown_monitor(IOContext& context) -> Task<void>
{
    using namespace std::chrono_literals;

    SignalSet sets{ context, signals::interrupt, signals::terminate };

    co_await sets.async_wait();

    spdlog::info("Received shutdown signal, stopping IOContext...");
    context.stop();
}

auto demo(IOContext& context) -> Task<void>
{
    using namespace std::chrono_literals;
    spdlog::info("demo started");    

    // 模拟一些持续的异步工作，直到接收到退出信号
    while (true) 
        co_await sleep_for(context, 1s);

    spdlog::info("demo completed");
}

int main(int argc, char* argv[])
{
    IOContext context{};

    co_spawn(context, demo(context));
    co_spawn(context, shutdown_monitor(context));

    context.run();

    spdlog::info("IOContext stopped, exiting...");

    return EXIT_SUCCESS;
}