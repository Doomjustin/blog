#include <cstdlib>

#include <blog.h>

auto shutdown_monitor(std::stop_source stop_source) -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    auto res = co_await signals.async_wait();

    if (!res) {
        log::error("signal wait failed: {}", res.error());
        co_return;
    }

    log::info("received signal: {}", *res);
    stop_source.request_stop();
}

auto user_input(net::ip::tcp::socket& socket) -> async::Task<>
{
    auto stream = fs::std_input();

    std::string buffer(4096, '\0');
    while (true) {
        auto res = co_await stream.async_read_some(buffer);
        if (!res) {
            log::error("read stdin failed: {}", res.error());
            co_return;
        }

        auto write_buffer = buffer.substr(0, *res);
        auto write_res = co_await async::write(socket, write_buffer);
        if (!write_res) {
            log::error("write socket failed: {}", write_res.error());
            co_return;
        }
    }
}

auto client(std::string_view address, std::uint16_t port) -> async::Task<>
{
    net::ip::tcp::endpoint target{ net::ip::Address::from_string(std::string(address)), port };
    net::ip::tcp::socket socket;
    socket.open(target.protocol());
    auto res = co_await socket.async_connect(target);
    if (!res) {
        log::error("connect socket failed: {}", res.error());
        co_return;
    }

    co_await async::spawn(user_input(socket));

    std::string buffer(4096, '\0');
    auto stream = fs::std_output();
    while (true) {
        auto res = co_await socket.async_read_some(buffer);
        if (!res) {
            log::error("read socket failed: {}", res.error());
            co_return;
        }

        auto message = buffer.substr(0, *res);
        auto write_res = co_await stream.async_write_some(message);
        if (!write_res) {
            log::error("write stdout failed: {}", write_res.error());
            co_return;
        }
    }
}

auto parse_port(const char* arg) -> std::optional<std::uint16_t>
{
    auto res = numeric_cast<std::uint16_t>(arg);
    if (!res || *res <= 0 || *res > 65535) {
        log::warning("invalid port argument '{}', using default port", arg);
        return std::nullopt;
    }

    return *res;
}

int main(int argc, char* argv[])
{
    std::uint16_t port = 12345;
    std::string address = "127.0.0.1";

    if (argc == 2)
        port = parse_port(argv[1]).value_or(12345);

    if (argc == 3) {
        address = argv[1];
        port = parse_port(argv[2]).value_or(12345);
    }

    async::IOContext context;
    std::stop_source stop_source;

    async::co_spawn(context, shutdown_monitor(stop_source));
    async::co_spawn(context, stop_source.get_token(), client(address, port));

    context.run();

    return EXIT_SUCCESS;
}