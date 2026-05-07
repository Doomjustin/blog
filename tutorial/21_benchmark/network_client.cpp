#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

constexpr int CLIENT_COUNT = 100;
constexpr int PING_PONG_PER_CLIENT = 10000;
constexpr int TOTAL_REQUESTS = CLIENT_COUNT * PING_PONG_PER_CLIENT;

auto run_client(std::uint16_t port) -> async::Task<>
{
    net::ip::tcp::socket sock{ net::ip::tcp::v4() };
    sock.connect(net::ip::tcp::endpoint{net::ip::AddressV4::loopback(), port});
    sock.option(net::ip::tcp::socket::no_delay{true});

    std::string payload = "Hello, Echo Benchmark!";
    std::string recv_buf(payload.size(), '\0');

    for (int i = 0; i < PING_PONG_PER_CLIENT; ++i) {
        co_await net::send(sock, async::buffer(payload));
        co_await net::receive(sock, async::buffer(recv_buf));
    }
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        log::error("usage: {} <port>", argv[0]);
        return EXIT_FAILURE;
    }

    auto port = static_cast<std::uint16_t>(std::atoi(argv[1]));
    log::info("压测目标: 127.0.0.1:{}", port);

    auto start = std::chrono::high_resolution_clock::now();

    async::run([port]() -> async::Task<> {
        std::vector<async::Task<>> clients;
        clients.reserve(CLIENT_COUNT);
        for (int i = 0; i < CLIENT_COUNT; ++i)
            clients.push_back(run_client(port));

        async::Scope group;
        for (auto& c : clients) group.spawn(std::move(c));
        co_await group.join();
    });

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start).count();

    log::info("完成 {} 次网络请求，总耗时: {} ms", TOTAL_REQUESTS, ms);
    return EXIT_SUCCESS;
}
