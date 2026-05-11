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
#include <functional>
#include <iostream>
#include <string>

#include <blog.h>

// 从终端读入用户输入并发给客户端
auto handle_terminal(net::ip::tcp::socket& socket, std::stop_token token) -> async::Task<>
{
    log::info("[client] 连接成功！请输入用户名：");

    auto input = fs::async_stdin();
    std::array<char, 1024> buffer;

    auto read_res = co_await input.async_read(async::buffer(buffer));
    if (!read_res || *read_res == 0) {
        log::error("[client] 读取用户名失败或输入为空。");
        co_return;
    }

    std::string_view name(buffer.data(), *read_res);

    auto send_res = co_await net::send(socket, async::buffer(name));
    if (!send_res) {
        log::error("[client] 发送用户名失败: {}", send_res.error());
        co_return;
    }

    while (!token.stop_requested()) {
        auto read_res = co_await async::stop_then(input.async_read(async::buffer(buffer)), token);
        if (!read_res || *read_res == 0) 
            break; // EOF or error

        auto line = std::string_view(buffer.data(), *read_res);
        // ignore empty lines to avoid sending unnecessary messages to the server
        if (line == "\n") continue;

        auto send_res = co_await net::send(socket, async::buffer(line));
        if (!send_res) {
            log::error("[input] 发送消息失败: {}", send_res.error());
            co_return;
        }
    }
}

// 处理服务器发来的消息并打印到终端
auto handle_server(net::ip::tcp::socket& socket, std::stop_token token) -> async::Task<>
{
    auto stream = socket.receive_stream();
    auto output = fs::async_stdout();

    while (!token.stop_requested()) {
        auto chunk = co_await stream.next();
        if (!chunk || chunk->data().empty()) {
            log::info("[server] connection closed.");
            break;
        }

        co_await async::stop_then(output.async_write(chunk->data()), token);
    }
}

auto session(net::ip::tcp::socket socket) -> async::Task<>
{
    co_await async::any(async::task(handle_terminal, std::ref(socket)), 
                        async::task(handle_server, std::ref(socket)));
}

auto client(std::string_view host, std::uint16_t port) -> async::Task<>
{
    try {
        async::this_coroutine::setup_buffer_ring(128);
        auto endpoint = net::ip::tcp::endpoint{ net::ip::Address::from_string(host), port };
        net::ip::tcp::socket socket{ endpoint.protocol() };
        socket.connect(endpoint);
        co_await session(std::move(socket));
    } catch (const std::exception& ex) {
        log::error("[client] 连接失败: {}", ex.what());
        async::stop();
    }
}

auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    co_await signals.async_wait();

    log::info("[client] shutting down...");
    async::stop();
}

// chat.client [host:127.0.0.1] [port:9090]
int main(int argc, char* argv[])
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 9090;

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