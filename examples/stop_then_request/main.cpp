// Demonstrates fine-grained, per-operation cancellation in a request-response loop.
//
// Pattern: wrapping each individual async op (send + receive) with the same
// stop_token so cancellation is detected at the earliest possible I/O boundary,
// rather than only at a single fixed point.
//
// Topology:
//   demo()
//   ├── co_spawn(echo_server(port))          — simple backdrop; no stop needed
//   └── co_await client(stop, port)          — sends requests in a loop
//         └── one_request(stop)
//               ├── stop_then(async_send_some, stop)    ← step 1
//               └── stop_then(async_receive_some, stop) ← step 2 (most likely cancel point)
//
// After ~1.5 s the stop_source fires.  The client is blocked at step 2 waiting
// for the server's echo.  It receives operation_canceled, logs the cancellation,
// and exits cleanly.  When demo() returns the event loop ends and the server is
// cleaned up automatically.

#include <array>
#include <chrono>
#include <cstdlib>
#include <stop_token>
#include <thread>

#include <blog.h>
#include <post.h>

namespace {

// ── server side ──────────────────────────────────────────────────────────────

// Plain echo session with artificial processing delay.
// This simulates real service latency (DB/remote RPC) and creates a stable
// window where client-side receive can be cancelled by stop_token.
auto session(net::ip::tcp::socket client) -> async::Task<>
{
    std::array<std::byte, 4096> buf{};
    while (true) {
        auto recv = co_await client.async_receive_some(buf);
        if (!recv || *recv == 0)
            co_return;

        co_await async::sleep_for(std::chrono::milliseconds{ 700 });

        if (auto sent = co_await net::send(client, std::span{ buf.data(), *recv }); !sent)
            co_return;
    }
}

// Simple accept loop — no stop_token required; the event loop ending on
// demo() return is enough to clean up all pending I/O.
auto echo_server(std::uint16_t& out_port) -> async::Task<>
{
    net::ip::tcp::endpoint endpoint{ net::ip::AddressV4::loopback(), 0 };
    net::ip::tcp::acceptor acceptor{ endpoint };

    if (auto ep = local_endpoint(acceptor))
        out_port = ep->port();   // signal the client that the server is ready

    while (true) {
        auto client = co_await acceptor.async_accept();
        if (!client) {
            log::error("[server] accept error: {}", client.error());
            continue;
        }
        async::co_spawn(session(std::move(*client)));
    }
}

// ── client side ──────────────────────────────────────────────────────────────

// Send one request and wait for the echo.  Both I/O steps are individually
// wrapped in stop_then so a stop request is honoured at the very next
// kernel interaction — no polling, no extra timeout machinery.
auto one_request(net::ip::tcp::socket& sock,
                 std::string_view message,
                 std::stop_token stop)
    -> async::Task<std::expected<std::string, std::error_code>>
{
    auto payload = async::buffer(message);

    // Step 1 – send.  Cancellable in case the kernel send buffer is full and
    // a stop is requested before the byte is accepted.
    auto sent = co_await async::stop_then(sock.async_send_some(payload), stop);
    if (!sent)
        co_return std::unexpected(sent.error());

    // Step 2 – receive echo.  The most likely cancel point: the coroutine
    // blocks here until the server replies, which may never happen if the
    // server is overloaded or the network is slow.
    std::array<std::byte, 256> buf{};
    auto recv = co_await async::stop_then(sock.async_receive_some(buf), stop);
    if (!recv)
        co_return std::unexpected(recv.error());

    co_return std::string{ reinterpret_cast<const char*>(buf.data()), *recv };
}

auto client(std::stop_token stop, const std::uint16_t& server_port) -> async::Task<>
{
    // Yield until the server has stored its ephemeral port.
    // Both coroutines run on the same thread so a single yield is enough,
    // but a loop is more robust against ordering surprises.
    while (server_port == 0)
        co_await async::sleep_for(std::chrono::milliseconds{ 1 });

    net::ip::tcp::endpoint endpoint{ net::ip::AddressV4::loopback(), server_port };
    net::ip::tcp::socket sock{ net::ip::tcp::v4() };
    sock.connect(endpoint);
    log::info("[client] connected to :{}", server_port);

    for (int i = 0; ; ++i) {
        auto msg = "request-" + std::to_string(i);
        auto result = co_await one_request(sock, msg, stop);

        if (!result) {
            const auto& ec = result.error();
            if (ec == std::errc::operation_canceled)
                log::info("[client] request #{} cancelled — exiting cleanly", i);
            else
                log::error("[client] request #{} failed: {}", i, ec);
            co_return;
        }

        log::info("[client] echo: {}", *result);

        // Keep the loop tight so there is almost always an in-flight receive.
        co_await async::sleep_for(std::chrono::milliseconds{ 50 });
    }
}

// ── demo entry point ──────────────────────────────────────────────────────────

auto demo() -> async::Task<>
{
    std::stop_source shutdown;

    // Simulate an external cancellation trigger: a SIGINT handler, a higher-
    // level deadline, or a user action would call request_stop() in real code.
    std::jthread timer{ [source = shutdown]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1500 });
        log::info("[timer] requesting stop");
        source.request_stop();
    } };

    std::uint16_t server_port = 0;

    // Server runs in background.  When client detects cancellation it returns,
    // demo() returns, and the event loop ends — cleaning up the server automatically.
    async::co_spawn(echo_server(server_port));

    // demo() blocks here until the client detects cancellation and returns.
    co_await client(shutdown.get_token(), server_port);

    log::info("[demo] completed cleanly");
}

} // namespace

int main()
{
    async::run(demo);
    return EXIT_SUCCESS;
}
