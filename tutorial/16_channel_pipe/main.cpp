#include <cstdlib>
#include <thread>

#include <blog.h>
#include <channel_pipe.h>

namespace {

auto demo_cross_thread_pipeline() -> void
{
    log::info("=== demo 1: channel_pipe cross-thread pipeline ===");

    async::IOContext ctx_net;
    async::IOContext ctx_worker;
    auto [tx, rx] = async::make_channel<int>(4, ctx_net, ctx_worker);

    auto receiver = [rx = std::move(rx)]() mutable -> async::Task<> {
        auto v = co_await rx.receive();
        if (v)
            log::info("worker recv: {}", *v);
        log::info("worker done");
    };

    auto sender = [tx = std::move(tx)]() mutable -> async::Task<> {
        auto r = co_await tx.send(42);
        if (!r) {
            log::info("sender stopped early: {}", r.error().message());
            co_return;
        }
        log::info("net send: 42");
        tx.close();
        log::info("net close sender");
    };

    async::co_spawn(receiver(), ctx_worker);
    std::jthread worker_thread([&ctx_worker] { ctx_worker.run(); });

    async::co_spawn(sender(), ctx_net);
    ctx_net.run();
}

} // namespace

int main()
{
    demo_cross_thread_pipeline();
    return EXIT_SUCCESS;
}
