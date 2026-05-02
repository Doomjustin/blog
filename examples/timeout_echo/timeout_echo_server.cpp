#include <array>
#include <cstdlib>
#include <iostream>

#include <blog.h>

namespace {

using namespace std::chrono_literals;

constexpr auto idle_timeout = 5s;

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    log::info("Client connected: {}", peer);

    std::array<std::byte, 4096> buffer{};

    while (true) {
        auto read_result = co_await async::timeout(
            client.async_receive_some(buffer),
            idle_timeout
        );

        if (!read_result) {
            if (read_result.error() == std::errc::timed_out)
                log::info("Client {} idle timeout, disconnecting", peer);
            else if (read_result.error() != std::errc::operation_canceled)
                log::error("Receive error from {}: {}", peer, read_result.error());

            co_return;
        }

        auto bytes = *read_result;
        if (bytes == 0) {
            log::info("Client disconnected: {}", peer);
            co_return;
        }

        auto write_result = co_await net::send(client, std::span{ buffer }.first(bytes));
        if (!write_result) {
            if (write_result.error() != std::errc::operation_canceled)
                log::error("Send error to {}: {}", peer, write_result.error());

            co_return;
        }
    }
}

auto echo_server(std::uint16_t port) -> async::Task<>
{
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), port };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    log::info("Listening on port {} (idle timeout: {}s)", port, idle_timeout.count());

    while (true) {
        net::ip::tcp::endpoint peer;
        auto client = co_await acceptor.async_accept(peer);

        if (!client) {
            if (client.error() == std::errc::operation_canceled)
                co_return;

            log::error("Accept error: {}", client.error());
            continue;
        }

        async::co_spawn(session(std::move(*client), peer));
    }
}

auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    co_await signals.async_wait();
    log::info("Shutting down...");
    async::stop();
}

auto print_usage(char* argv0) -> int
{
    std::cerr << "Usage: " << argv0 << " <port>\n";
    std::cerr << "Example: " << argv0 << " 12345\n";
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
        return print_usage(argv[0]);

    try {
        auto port = static_cast<std::uint16_t>(std::stoi(argv[1]));

        async::co_spawn(shutdown_monitor());
        async::run(1, echo_server, port);
    }
    catch (const std::exception& ex) {
        log::error("timeout_echo_server failed: {}", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
