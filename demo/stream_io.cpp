#include <cstdlib>

#include <spdlog/spdlog.h>

#include "async.h"
#include "async_operations.h"
#include "co_spawn.h"
#include "io_context.h"
#include "ip/tcp.h"
#include "task.h"
#include "timeout.h"

auto session(ip::tcp::socket<IOContext> client) -> Task<>
{
    auto stream = client.receive_stream();

    while (true) { 
        // 从stream里读取数据；如果这个操作失败了（比如客户端断开了连接），就退出这个session
        auto read_result = co_await stream.next();

        if (!read_result) {
            spdlog::warn("Failed to read from client {}: {}", client.native_handle(), read_result.error().message());
            co_return;
        }

        auto received = read_result->data();
        if (received.empty()) {
            spdlog::info("Client {} disconnected", client.native_handle());
            co_return;
        }

        spdlog::info("Received {} bytes from client {}", received.size(), client.native_handle());

        using namespace std::literals::chrono_literals;
        auto write_result = co_await timeout(async_write(client, received), 1ms);
        if (!write_result) {
            if (write_result.error() == std::errc::timed_out)
                spdlog::warn("Write to client {} timed out", client.native_handle());
            else
                spdlog::warn("Failed to write to client {}: {}", client.native_handle(), write_result.error().message());
            
            co_return;
        }
    }
}

auto async_main() -> Task<>
{
    auto& context = this_coroutine::context();
    context.setup_buffer_ring(128, 4096);

    auto endpoint = ip::tcp::endpoint{ ip::AddressV6::loopback(), 12345 };
    spdlog::info("Server listening on {}:{}", endpoint.address().to_string(), endpoint.port());

    auto acceptor = ip::tcp::acceptor{ context, endpoint, true };

    auto client_endpoint = ip::tcp::endpoint{};
    while (true) {
        auto client = co_await acceptor.async_accept(client_endpoint);
        if (!client) {
            spdlog::warn("Failed to accept client connection: {}", client.error().message());
            continue;
        }

        spdlog::info("Accepted connection from {}:{}", client_endpoint.address().to_string(), client_endpoint.port());
        co_spawn(session(std::move(*client)));
    }
}

int main(int argc, char* argv[])
{
    spdlog::set_pattern("[%H:%M:%S] [%t] [%l] %v");

    async::run(4, async_main);

    return EXIT_SUCCESS;
}