#include <cstdlib>
#include <iostream>
#include <string_view>

#include <blog.h>

namespace {

// Pre-allocated message to demonstrate zero-copy semantics
constexpr std::string_view payload = "Zero-copy message from io_uring SEND_ZC\n";

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    log::info("Client connected: {}", peer);

    for (int i = 0; i < 3; ++i) {
        // Wrap buffer with zero_copy() tag to use IORING_OP_SEND_ZC
        // This signals the kernel "keep my buffer valid until notif CQE"
        auto send_result = co_await client.async_send_some(net::zero_copy(payload));

        if (!send_result) {
            if (send_result.error() != std::errc::operation_canceled)
                log::error("Zero-copy send error to {}: {}", peer, send_result.error());

            co_return;
        }

        log::info("Sent {} bytes (zero-copy) to {}", *send_result, peer);
    }

    log::info("Client disconnected: {}", peer);
}

auto zero_copy_server(std::uint16_t port) -> async::Task<>
{
    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), port };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    log::info("Zero-copy server listening on port {}", port);

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
    std::cerr << "Example: " << argv0 << " 8081\n";
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
        async::run(1, zero_copy_server, port);
    }
    catch (const std::exception& ex) {
        log::error("zero_copy_server failed: {}", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
