#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include <blog.h>

namespace {

auto client(std::string_view host, std::uint16_t port, std::string_view message) -> async::Task<>
{
	auto endpoint = net::ip::tcp::endpoint::from_string(host, port);

	auto socket = net::ip::tcp::socket{ endpoint.protocol() };
	socket.connect(endpoint);

	if (auto local = local_endpoint(socket), remote = remote_endpoint(socket); local && remote)
		log::info("Connected: {} -> {}", *local, *remote);

	auto write_result = co_await net::send(socket, async::buffer(message));
	if (!write_result) {
		log::error("Failed to send request: {}", write_result.error());
		co_return;
	}

	log::info("Sent {} bytes", *write_result);

	std::string response(message.size(), '\0');
	auto response_buffer = async::buffer(response);

	auto read_result = co_await net::receive(socket, response_buffer);
	if (!read_result) {
		log::error("Failed to receive response: {}", read_result.error());
		co_return;
	}

	log::info("Received {} bytes", *read_result);
	std::cout << response << '\n';
}

auto print_usage(char* argv0) -> int
{
	std::cerr << "Usage: " << argv0 << " <host> <port> <message>\n";
	std::cerr << "Example: " << argv0 << " 127.0.0.1 12345 hello\n";
	return EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[])
{
	if (argc != 4)
		return print_usage(argv[0]);

	try {
		auto host = std::string_view{ argv[1] };
        auto port_result = numeric_cast<std::uint16_t>(argv[2]);
        if (!port_result)
            throw std::system_error(port_result.error(), "invalid port");
        auto port = *port_result;
		auto message = std::string_view{ argv[3] };

		async::run(1, client, host, port, message);
	}
	catch (const std::exception& ex) {
		log::error("tcp_client failed: {}", ex.what());
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
