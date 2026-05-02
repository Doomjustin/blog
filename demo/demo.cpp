#include <cstdlib>

#include <spdlog/spdlog.h>

#include <blog.h>

auto session(net::ip::tcp::socket client) -> async::Task<>
{
    auto stream = client.receive_stream();

    while (true) { 
        // 从stream里读取数据；如果这个操作失败了（比如客户端断开了连接），就退出这个session
        auto read_result = co_await stream.next();

        if (!read_result) {
            log::warning("Failed to read from client {}: {}", client.native_handle(), read_result.error());
            co_return;
        }

        auto received = read_result->data();
        if (received.empty()) {
            log::info("Client {} disconnected", client.native_handle());
            co_return;
        }

        log::info("Received {} bytes from client {}", received.size(), client.native_handle());
        log::info("Data: {}", as_string(received));

        using namespace std::literals::chrono_literals;
        auto write_result = co_await async::timeout(net::send(client, received), 1ms);
        if (!write_result) {
            if (write_result.error() == std::errc::timed_out)
                log::warning("Write to client {} timed out", client.native_handle());
            else
                log::warning("Failed to write to client {}: {}", client.native_handle(), write_result.error());
            
            co_return;
        }
    }
}

auto echo() -> async::Task<>
{
    async::setup_buffer_ring(128, 4096);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV6::loopback(), 12345 };
    log::info("Server listening on {}", endpoint);

    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    // 验证下local_endpoint()接口是否能正确返回绑定的地址信息
    auto server_endpoint = local_endpoint(acceptor);
    log::info("Acceptor local endpoint: {}", *server_endpoint);

    net::ip::tcp::endpoint client_endpoint{};
    while (true) {
        auto client = co_await acceptor.async_accept(client_endpoint);
        if (!client) {
            if (client.error() == std::errc::operation_canceled) {
                log::info("Acceptor has been stopped, exiting echo server...");
                co_return;
            }
            
            log::warning("Failed to accept connection: {}", client.error());
            continue;
        }

        log::info("Accepted connection from {}", client_endpoint);

        async::co_spawn(session(std::move(*client)));
    }
}

auto shutdown_monitor() -> async::Task<void>
{
    async::SignalSet sets{ async::signals::interrupt, async::signals::terminate };
    co_await sets.async_wait();

    log::info("Received shutdown signal, stopping IOContext...");
    async::stop();
}

int main(int argc, char* argv[])
{
    auto worker_count = std::thread::hardware_concurrency() == 0 ? 4 : std::thread::hardware_concurrency();
    async::co_spawn(shutdown_monitor());
    async::run(worker_count, echo);

    return EXIT_SUCCESS;
}