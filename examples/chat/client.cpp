// Interactive TCP chat client
//
// Usage: ./chat_client [host] [port]   (defaults: 127.0.0.1 9090)
//
// Workflow:
//   1. Connect to the chat server.
//   2. Enter your username when prompted (first line sent).
//   3. Type messages and press Enter to send.
//   4. Messages from other users appear inline.
//   5. Press Ctrl+D (or Ctrl+C) to exit.
//
// Concurrency model:
//   - A co_spawned `receiver` task continuously reads server output and
//     prints it to stdout.  When the server closes the connection it calls
//     async::stop(), which cancels the in-flight run_on_thread call in the
//     sender loop, making the main coroutine exit cleanly.
//   - The sender loop calls run_on_thread for each blocking getline so the
//     IOContext is never blocked on stdin.

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

#include <async/task.h>

#include <blog.h>
#include <async/run_on_thread.h>

// namespace {

// // ── Receiver task ─────────────────────────────────────────────────────────────

// // Reads messages arriving from the server and prints them.
// // On disconnect (or error), calls async::stop() so the sender loop unblocks.
// auto receiver(net::ip::tcp::socket& sock) -> async::Task<>
// {
//     auto stream = sock.receive_stream();
//     std::string pending;

//     while (true) {
//         auto chunk = co_await stream.next();
//         if (!chunk) {
//             if (chunk.error() != std::errc::operation_canceled)
//                 std::cerr << std::format("[!] Connection error: {}\n", chunk.error().message());
//             break;
//         }
//         if (chunk->data().empty()) {
//             std::cerr << "[!] Server closed the connection.\n";
//             break;
//         }

//         pending += as_string(chunk->data());

//         // Print complete lines immediately; hold partial lines for next chunk.
//         while (true) {
//             auto nl = pending.find('\n');
//             if (nl == std::string::npos) break;
//             std::cout << pending.substr(0, nl + 1);
//             pending.erase(0, nl + 1);
//         }
//         std::cout.flush();
//     }

//     async::stop(); // unblock the sender's run_on_thread via IOContext cancellation
// }

// // ── Sender / main coroutine ───────────────────────────────────────────────────

// auto client(std::string host, uint16_t port) -> async::Task<>
// {
//     // ── Connect ──────────────────────────────────────────────────────────────
//     auto endpoint = net::ip::tcp::endpoint::from_string(host, port);
//     auto sock     = net::ip::tcp::socket{ endpoint.protocol() };
//     try {
//         sock.connect(endpoint);
//     }
//     catch (const std::exception& ex) {
//         log::error("connect failed: {}", ex.what());
//         co_return;
//     }

//     if (auto loc = local_endpoint(sock), rem = remote_endpoint(sock); loc && rem)
//         log::info("connected {} -> {}", *loc, *rem);

//     // Buffer ring is required by ReceiveStream.
//     async::this_coroutine::setup_buffer_ring(64);

//     // ── Launch background receiver ───────────────────────────────────────────
//     // receiver() borrows `sock`; it lives as long as this coroutine frame,
//     // which is kept alive by async::run() until all tasks complete.
//     async::co_spawn(receiver(sock));

//     // ── Sender loop ──────────────────────────────────────────────────────────
//     // Each getline is offloaded to a detached OS thread so the IOContext
//     // keeps running.  When async::stop() is called (by receiver or Ctrl+C),
//     // the io_context cancels the in-flight run_on_thread operation and
//     // await_resume() returns unexpected(operation_canceled).
//     while (true) {
//         auto result = co_await async::run_on_thread([]() -> std::string {
//             std::string line;
//             if (!std::getline(std::cin, line)) return ""; // EOF
//             return line;
//         });

//         if (!result) break; // operation_canceled: async::stop() was called

//         auto& line = *result;
//         if (line.empty()) {
//             // Empty input on EOF (getline returned "")
//             if (std::cin.eof()) break;
//             continue; // user pressed Enter on an empty line — skip
//         }

//         auto msg = line + "\n";
//         auto send_result = co_await net::send(sock, async::buffer(msg));
//         if (!send_result) {
//             log::error("send failed: {}", send_result.error());
//             break;
//         }
//     }

//     async::stop();
// }

// auto run(std::string host, uint16_t port) -> async::Task<>
// {
//     async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
//     async::co_spawn([](async::SignalSet signals) -> async::Task<> {
//         co_await signals.async_wait();
//         async::stop();
//     }(std::move(signals)));

//     co_await client(std::move(host), port);
// }

// } // namespace

// int main(int argc, char* argv[])
// {
//     std::string host = "127.0.0.1";
//     uint16_t    port = 9090;

//     if (argc >= 2) host = argv[1];
//     if (argc >= 3) {
//         auto p = std::atoi(argv[2]);
//         if (p > 0 && p < 65536)
//             port = static_cast<uint16_t>(p);
//     }

//     // Disable stdout buffering so messages from the receiver appear immediately.
//     std::cout.setf(std::ios::unitbuf);

//     async::run([&]{ return run(host, port); });
// }

auto input(async::Channel<std::string>& channel) -> async::Task<>
{
    while (true) {
        std::string line;
        if (!std::getline(std::cin, line)) break; // EOF

        auto res = co_await channel.send(std::move(line));
        if (!res) {
            log::error("[input] send failed: {}", res.error());
            co_return;
        }
    }
}

auto session(net::ip::tcp::socket socket, std::string_view name) -> async::Task<>
{
    async::Channel<std::string> channel{ 64 };
    std::jthread input_thread([&] { async::run(input, channel); });

    while (auto res = co_await channel.receive()) {
        auto& line = *res;
        if (line.empty()) continue; // skip empty lines

        auto msg = line + "\n";

        std::cout << format("[{}] sending: {}", name, line);
        auto send_result = co_await net::send(socket, async::buffer(msg));
        if (!send_result) {
            log::error("[session] send failed: {}", send_result.error());
            break;
        }
    }
}

auto client(std::string_view host, std::uint16_t port) -> async::Task<>
{
    auto server_endpoint = net::ip::tcp::endpoint::from_string(host, port);
    auto socket = net::ip::tcp::socket{ server_endpoint.protocol() };
    
    try {
        socket.connect(server_endpoint);
    }
    catch (const std::exception& ex) {
        log::error("[client] 连接失败: {}", ex.what());
        co_return;
    }

    if (auto loc = local_endpoint(socket), rem = remote_endpoint(socket); loc && rem)
        log::info("[client] 已连接 {} -> {}", *loc, *rem);

    std::string name;
    std::cout << "请输入用户名: ";
    std::getline(std::cin, name);

    co_await session(std::move(socket), name);
}

auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    co_await signals.async_wait();

    log::info("[client] shutting down...");
    async::stop();
}

// chat.client [host:127.0.0.1] [port:9093]
int main(int argc, char* argv[])
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 9093;

    if (argc >= 2) host = argv[1];

    if (argc >= 3) {
        auto res = numeric_cast<std::uint16_t>(argv[2]);
        if (!res)
            std::cerr << format("Invalid port number '{}': {}'\n", argv[2], res.error());
        else
            port = *res;
    }

    log::info("[client] 正在连接到 {}:{}...", host, port);

    async::co_spawn(shutdown_monitor());
    async::run(client, host, port);

    log::info("[client] 退出chat.");
    return EXIT_SUCCESS;
}