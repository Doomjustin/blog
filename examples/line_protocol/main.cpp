#include <cstdlib>
#include <iostream>

#include <blog.h>

namespace {

auto process_line(std::span<const std::byte> line) -> std::string
{
    auto view = as_string(line);

    // 移除末尾 \r\n
    while (!view.empty() && (view.back() == '\r' || view.back() == '\n'))
        view.remove_suffix(1);

    return std::string{ view };
}

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    log::info("Client connected: {}", peer);

    auto stream = client.receive_stream();
    while (true) {
        auto read_result = co_await stream.next();

        if (!read_result) {
            if (read_result.error() != std::errc::operation_canceled)
                log::error("Receive error from {}: {}", peer, read_result.error());

            co_return;
        }

        auto buffer = read_result->data();
        if (buffer.empty()) {
            log::info("Client disconnected: {}", peer);
            co_return;
        }

        auto line = process_line(buffer);
        log::info("Received from {}: {}", peer, line);

        // 回显这一行
        auto response = line + "\n";
        auto write_result = co_await net::send(client, async::buffer(response));
        if (!write_result) {
            if (write_result.error() != std::errc::operation_canceled)
                log::error("Send error to {}: {}", peer, write_result.error());

            co_return;
        }
    }
}

auto line_server(std::uint16_t port) -> async::Task<>
{
    async::setup_buffer_ring(128, 4096);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), port };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    log::info("Line protocol server listening on port {}", port);

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
        async::run(line_server, port);
    }
    catch (const std::exception& ex) {
        log::error("line_server failed: {}", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
