#include <cstdlib>
#include <iostream>

#include <spdlog/spdlog.h>

#include "buffer.h"
#include "co_spawn.h"
#include "io_context.h"
#include "ip/tcp.h"
#include "signals.h"
#include "task.h"
#include "timeout.h"

auto shutdown_monitor(IOContext& context) -> Task<void>
{
    using namespace std::chrono_literals;

    SignalSet sets{ context, signals::interrupt, signals::terminate };

    co_await sets.async_wait();

    spdlog::info("Received shutdown signal, stopping IOContext...");
    context.stop();
}

auto session(ip::tcp::socket<IOContext> client) -> Task<>
{
    auto data = std::string(1024, '\0');

    while (true) {
        // 1. 异步读取，协程挂起，零线程阻塞
        // 如果需要的话，你也可以套一个timeout。不过不要忘了除了timedout错误
        auto read_result = co_await client.async_read_some(buffer(data));
        if (!read_result) {
            spdlog::warn("Failed to read from client {}: {}", client.native_handle(), read_result.error().message());
            co_return;
        }

        auto bytes_read = *read_result;
        if (bytes_read == 0) {
            spdlog::info("Client {} disconnected", client.native_handle());
            co_return;
        }

        spdlog::warn("Data from client {}: {}", client.native_handle(), data);

        auto write_buffer = buffer(data.substr(0, bytes_read));

        // 2. 利用 std::span 提取有效数据视图，异步写回
        using namespace std::literals::chrono_literals;
        auto write_result = co_await timeout(client.async_write_some(write_buffer), 5s);
        if (!write_result) {
            if (write_result.error() == std::errc::timed_out)
                spdlog::warn("Write to client {} timed out", client.native_handle());
            else
                spdlog::warn("Failed to write to client {}: {}", client.native_handle(), write_result.error().message());

            co_return;
        }
    }
}

auto echo(IOContext& context) -> Task<>
{
    // auto endpoint = ip::tcp::endpoint::from_string("127.0.0.1", 12345);
    auto endpoint = ip::tcp::endpoint{ ip::AddressV6::loopback(), 12345 };
    std::cout << "Server listening on " << endpoint << "\n";

    auto acceptor = ip::tcp::acceptor{ context, endpoint };

    auto client_endpoint = ip::tcp::endpoint{};
    while (true) {
        auto client = co_await acceptor.async_accept(client_endpoint);
        if (!client) {
            spdlog::warn("Failed to accept client connection: {}", client.error().message());
            continue;
        }

        spdlog::info("Accepted connection from {}:{}", client_endpoint.address().to_string(), client_endpoint.port());
        co_spawn(context, session(std::move(*client)));
    }
}

int main(int argc, char* argv[])
{
    IOContext context;

    co_spawn(context, echo(context));
    co_spawn(context, shutdown_monitor(context));

    context.run();

    return EXIT_SUCCESS;
}