#include "ip/tcp.h"

#include <cstdlib>
#include <iostream>

#include <spdlog/spdlog.h>

#include "co_spawn.h"
#include "io_context.h"
#include "task.h"

auto session(ip::tcp::socket<IOContext> client) -> Task<>
{
    auto buffer = std::array<std::byte, 1024>{};

    while (true) {
        auto read_result = co_await client.async_read_some(buffer);
        if (!read_result) {
            spdlog::warn("Failed to read from client {}: {}", client.native_handle(), read_result.error().message());
            co_return;
        }

        auto bytes_read = *read_result;
        if (bytes_read == 0) {
            spdlog::info("Client {} disconnected", client.native_handle());
            co_return;
        }

        spdlog::info("Read {} bytes from client {}", bytes_read, client.native_handle());
        std::string data{ reinterpret_cast<const char*>(buffer.data()), bytes_read };
        spdlog::warn("Data from client {}: {}", client.native_handle(), data);

        auto write_buffer = std::span{ buffer }.first(bytes_read);
        auto write_result = co_await client.async_write_some(write_buffer);
        if (!write_result) {
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

    while (true) {
        auto client = co_await acceptor.async_accept();
        if (!client) {
            spdlog::warn("Failed to accept client connection: {}", client.error().message());
            continue;
        }

        co_spawn(context, session(std::move(*client)));
    }
}

int main(int argc, char* argv[])
{
    IOContext context;

    co_spawn(context, echo(context));

    context.run();

    return EXIT_SUCCESS;
}