#include <array>
#include <cstdlib>
#include <system_error>

#include "this_coroutine.h"

#include <blog.h>

static constexpr std::string_view response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";

constexpr auto is_peer_shutdown(const std::error_code& ec) -> bool
{
    return ec == std::errc::connection_reset || 
           ec == std::errc::broken_pipe ||
           ec == std::errc::connection_aborted ||
           ec == std::errc::operation_canceled;
}

auto session(net::ip::tcp::socket socket) -> async::Task<>
{
    socket.option(net::ip::tcp::socket::no_delay(true));

    std::array<std::byte, 1024> buffer{};

    while (true) {
        auto read_result = co_await socket.async_receive_some(buffer);
        if (!read_result) {
            if (!is_peer_shutdown(read_result.error()))
                log::error("Failed to read from client: {}", read_result.error());

            co_return;
        }

        if (*read_result == 0)
            co_return;

        auto bytes_written = co_await net::send(socket, async::buffer(response));
        if (!bytes_written) {
            if (!is_peer_shutdown(bytes_written.error()))
                log::error("Failed to write to client: {}", bytes_written.error());

            co_return;
        }
    }
}

auto http() -> async::Task<>
{
    async::this_coroutine::setup_entries(4096);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::loopback(), 12345 };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    net::ip::tcp::endpoint client_endpoint;
    while (true) {
        auto client = co_await acceptor.async_accept(client_endpoint);
        if (!client) {
            if (client.error() == std::errc::operation_canceled) {
                log::info("Acceptor has been stopped, exiting http server");
                co_return;
            }

            log::error("Failed to accept client connection: {}", client.error());
            continue;
        }

        co_spawn(session(std::move(*client)));
    }
}

auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet sets{ async::signals::interrupt, async::signals::terminate };

    co_await sets.async_wait();
    log::info("Received shutdown signal, stopping IOContext...");

    async::stop();
}

int main(int argc, char* argv[])
{
    async::co_spawn(shutdown_monitor());
    async::run(20, http);

    return EXIT_SUCCESS;
}