#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

#include <blog.h>

namespace {

struct HttpResponse {
    std::string status_line;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};

auto make_plain_text_response(std::string_view text) -> HttpResponse
{
    HttpResponse response{};
    response.status_line = "HTTP/1.1 200 OK\r\n";
    response.body.assign(text.begin(), text.end());

    response.headers["Content-Type"] = "text/plain; charset=utf-8";
    response.headers["Content-Length"] = std::to_string(response.body.size());
    response.headers["Connection"] = "keep-alive";
    response.headers["Server"] = "blog-demo/1.0";

    return response;
}

auto send_http_response(net::ip::tcp::socket& socket, const net::ip::tcp::endpoint& peer) -> async::Task<>
{
    auto response = make_plain_text_response("Hello, World!");

    // Serialize map headers once, then scatter-gather all chunks in one writev.
    std::vector<std::string> header_lines{};
    header_lines.reserve(response.headers.size() + 1);
    for (const auto& [key, value] : response.headers)
        header_lines.emplace_back(format("{}: {}\r\n", key, value));

    header_lines.emplace_back("\r\n");

    std::vector<std::span<const std::byte>> parts{};
    parts.reserve(1 + header_lines.size() + 1);
    parts.emplace_back(async::buffer(response.status_line));
    for (const auto& line : header_lines)
        parts.emplace_back(async::buffer(line));

    parts.emplace_back(async::buffer(response.body));

    // Single writev syscall sends all parts atomically
    auto send_result = co_await socket.async_send_some(parts);
    if (!send_result) {
        if (send_result.error() != std::errc::operation_canceled)
            log::error("Send error to {}: {}", peer, send_result.error());
        co_return;
    }

    log::info("Sent {} bytes to {}", *send_result, peer);
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

        co_await send_http_response(client, peer);
    }
}

auto scatter_gather_server(std::uint16_t port) -> async::Task<>
{
    async::setup_buffer_ring(128, 4096);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), port };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    log::info("Scatter-gather server listening on port {}", port);

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
    std::cerr << "Example: " << argv0 << " 8080\n";
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
        async::run(1, scatter_gather_server, port);
    }
    catch (const std::exception& ex) {
        log::error("scatter_gather_server failed: {}", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
