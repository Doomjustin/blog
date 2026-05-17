#include <cstdlib>

#include <blog.h>

auto shutdown_monitor(std::stop_source stop_source) -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    auto res = co_await signals.async_wait();

    if (!res) {
        log::error("signal wait failed: {}", res.error());
        co_return;
    }

    log::info("received signal: {}", *res);
    stop_source.request_stop();
}

auto session(net::ip::tcp::socket socket) -> async::Task<>
{
    std::string buffer(4096, '\0');

    while (true) {
        auto read_res = co_await socket.async_read_some(buffer);
        if (!read_res) {
            log::error("read failed: {}", read_res.error());
            break;
        }

        if (*read_res == 0) {
            log::info("client {} disconnected", *net::remote_endpoint(socket));
            break;
        }

        std::string_view write_buffer{ buffer.data(), *read_res };
        auto write_res = co_await socket.async_write_some(write_buffer);
        if (!write_res) {
            log::error("write failed: {}", write_res.error());
            break;
        }
    }
}

auto echo() -> async::Task<>
{
    net::ip::tcp::endpoint endpoint{ net::ip::AddressV4::any(), 12345 };
    net::ip::tcp::acceptor acceptor{ endpoint };
    // 这里地址查询一定会成功，所以直接解引用 expected 获取地址信息并打印。
    log::info("echo server listening on {}", *net::local_endpoint(acceptor));

    while (true) {
        auto res = co_await acceptor.async_accept();
        if (!res) {
            // 在这个简单的case中，一旦出错就退出，观察错误信息即可。
            log::error("accept failed: {}", res.error());
            co_return;
        }

        co_await async::spawn(session(std::move(*res)));
    }
}

int main(int argc, char* argv[])
{
    async::IOContext context;
    std::stop_source stop_source;

    async::co_spawn(context, shutdown_monitor(stop_source));
    async::co_spawn(context, stop_source.get_token(), echo());

    context.run();

    return EXIT_SUCCESS;
}