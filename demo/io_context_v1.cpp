#include <cassert>
#include <cstdlib>

#include <liburing.h>
#include <spdlog/spdlog.h>

#include "task.h"
#include "operation.h"
#include "sleep_for.h"
#include "exceptions.h"
#include "co_spawn.h"


// 只支持 core per thread 模型，所以io_context本身不需要考虑线程安全问题
class IOContext {
public:
    explicit IOContext(unsigned entries = 1024)
    {
        if (auto res = ::io_uring_queue_init(entries, &ring_, 0); res < 0)
            throw_system_error(-res, "io_uring_queue_init");            
    }

    IOContext(const IOContext&) = delete;
    auto operator=(const IOContext&) -> IOContext& = delete;

    // 为了简化实现，我们不支持移动
    IOContext(IOContext&& other) noexcept = delete;
    auto operator=(IOContext&&) -> IOContext& = delete;

    ~IOContext()
    {
        ::io_uring_queue_exit(&ring_);
    }

    void run()
    {
        ::io_uring_cqe* cqe{ nullptr };

        while (!stopped_.load(std::memory_order_relaxed) && outstanding_works_ > 0)
        {
            if (::io_uring_submit_and_wait(&ring_, 1) < 0)
                throw_system_error("io_uring_submit_and_wait");

            unsigned head;
            unsigned count{ 0 };

            io_uring_for_each_cqe(&ring_, head, cqe) {
                ++count;

                if (cqe->user_data != 0) {
                    auto* op = reinterpret_cast<Operation*>(cqe->user_data);
                    op->complete(cqe->res, cqe->flags);
                }
            }

            if (count > 0) {
                outstanding_works_ -= count;
                ::io_uring_cq_advance(&ring_, count);
            }
        }
    }

    // 给外部组件提供便捷的接口来获取 SQE，以便提交 I/O 请求
    // 这个接口会自动增加 outstanding_operations_ 的计数，
    // 以便在 run() 方法中能够正确地判断是否还有未完成的工作。
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
        // TODO: 显然，只有这个变量是不足以完整实现 stop 功能的
        // 还需要考虑如何取消已经提交但尚未完成的 I/O 请求，以及如何通知正在等待的 run() 方法尽快返回。
        // 我们将在未来的版本中逐步完善这个功能。
        stopped_.store(true, std::memory_order_relaxed);
    }

    auto ring() noexcept -> ::io_uring*
    {
        return &ring_;
    }

    [[nodiscard]] 
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
    ::io_uring ring_{};

    // 只用来追踪io_context之外的操作，并不需要用户主动来使用相关的接口
    std::size_t outstanding_works_{ 0 };
    // stop会被跨线程调用，所以需要使用原子变量来保证线程安全
    std::atomic<bool> stopped_{ false };
};

auto demo(IOContext& context) -> Task<void>
{
    using namespace std::chrono_literals;
    
    spdlog::info("before sleep");

    co_await sleep_for(context, 5s);

    spdlog::info("after sleep");
}

int main(int argc, char* argv[])
{
    IOContext context{};
    co_spawn(context, demo(context));

    context.run();
    return EXIT_SUCCESS;
}