// Multi-user TCP chat server
//
// Protocol: line-based, terminated by '\n'.
//   - First line sent by client: username (e.g. "Alice\n")
//   - Subsequent lines: chat messages
//   - Server broadcasts each message to all other connected clients
//
// Usage: ./chat_server [port]   (default port: 9090)

#include <cstdlib>
#include <format>
#include <string>
#include <unordered_map>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

// ── Room ──────────────────────────────────────────────────────────────────────

// Tracks all connected clients.  Each client owns a Channel<string> "outbox"
// that the per-session writer coroutine drains and sends over the socket.
//
// All methods run on the single IOContext thread → no locking required.
struct Room {
    struct Client {
        std::string name;
        async::Channel<std::string>* outbox; // owned by the session coroutine
    };

    uint64_t next_id_{ 1 };
    std::unordered_map<uint64_t, Client> clients_;

    auto join(std::string name, async::Channel<std::string>& outbox) -> uint64_t
    {
        uint64_t id = next_id_++;
        clients_[id] = { .name=std::move(name), .outbox=&outbox };
        return id;
    }

    void leave(uint64_t id) { clients_.erase(id); }

    auto name_of(uint64_t id) const -> std::string
    {
        auto it = clients_.find(id);
        return it != clients_.end() ? it->second.name : std::string{ "unknown" };
    }

    // Deliver msg to every client except the sender.
    // co_await-based so back-pressure is handled naturally.
    auto broadcast(uint64_t from_id, std::string msg) -> async::Task<>
    {
        for (auto& [id, client] : clients_) {
            if (id == from_id) continue;
            
            auto res = co_await async::timeout(client.outbox->send(msg), 100ms);
            if (!res) {
                log::warning("[room] Client {} is too slow or disconnected; closing its channel", client.name);
                client.outbox->close();
            }

        }
    }

    // Deliver a system message to all clients (including the trigger if still registered).
    auto announce(std::string msg) -> async::Task<>
    {
        for (auto& [id, client] : clients_) {
            auto res = co_await async::timeout(client.outbox->send(msg), 100ms);
            if (!res) {
                log::warning("[room] Client {} is too slow or disconnected; closing its channel", client.name);
                client.outbox->close();
            }
        }
    }
};

// ── Writer task ───────────────────────────────────────────────────────────────

// Drains `outbox` and writes each string to `sock`.
// Exits when the channel is closed (client disconnected or session ending).
auto write_loop(net::ip::tcp::socket& sock,
                async::Channel<std::string>& outbox) -> async::Task<>
{
    while (auto msg = co_await outbox.receive()) {
        auto result = co_await net::send(sock, async::buffer(*msg));
        if (!result) co_return; // socket error; session will clean up
    }
}

// ── Helper: parse lines from a pending string buffer ─────────────────────────

// Extracts one complete line (up to '\n') from `pending`, strips '\r',
// stores it in `line`, and erases the consumed bytes from `pending`.
// Returns true if a line was extracted, false if no '\n' found yet.
auto extract_line(std::string& pending, std::string& line) -> bool
{
    auto pos = pending.find('\n');
    if (pos == std::string::npos) return false;

    line = pending.substr(0, pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    pending.erase(0, pos + 1);
    return true;
}

// ── Session ───────────────────────────────────────────────────────────────────

auto session(net::ip::tcp::socket sock,
             net::ip::tcp::endpoint peer,
             Room& room) -> async::Task<>
{
    log::info("[server] connected: {}", peer);

    // Outgoing message queue for this client.
    // Capacity 64: enough to buffer a burst before applying back-pressure.
    async::Channel<std::string> outbox{ 64 };

    // Use a TaskGroup so the writer task cannot outlive the socket.
    async::TaskGroup group;
    group.spawn(write_loop(sock, outbox));

    auto stream = sock.receive_stream();
    std::string pending;
    std::string line;

    // ── Handshake: read username ─────────────────────────────────────────────
    std::string username;
    while (username.empty()) {
        auto chunk = co_await stream.next();
        if (!chunk || chunk->data().empty()) {
            log::info("[server] {} disconnected before sending username", peer);
            outbox.close();
            co_await group.join();
            co_return;
        }
        pending += as_string(chunk->data());
        if (extract_line(pending, line) && !line.empty())
            username = line;
    }

    // Send a welcome banner to the new client.
    co_await outbox.send(std::format("[server] Welcome, {}! ({} users online)\n",
                                     username, room.clients_.size() + 1));

    uint64_t id = room.join(username, outbox);
    co_await room.announce(std::format("[server] {} joined the room.\n", username));
    log::info("[server] {} ({}) joined the room", username, peer);

    // ── Main read loop ───────────────────────────────────────────────────────
    while (true) {
        auto chunk = co_await stream.next();
        if (!chunk) {
            if (chunk.error() != std::errc::operation_canceled)
                log::error("[server] recv error from {}: {}", peer, chunk.error());
            break;
        }
        if (chunk->empty()) break; // EOF

        pending += as_string(chunk->data());

        while (extract_line(pending, line)) {
            if (line.empty()) continue; // skip blank lines
            auto msg = std::format("[{}] {}\n", username, line);
            log::info("{}", msg);
            co_await room.broadcast(id, msg);
        }
    }

    // ── Cleanup ──────────────────────────────────────────────────────────────
    room.leave(id);
    outbox.close(); // signals write_loop to exit after draining
    co_await room.announce(std::format("[server] {} left the room.\n", username));
    log::info("[server] {} ({}) disconnected", username, peer);

    co_await group.join(); // wait for writer to finish draining
}

// ── Server ────────────────────────────────────────────────────────────────────

auto server(uint16_t port) -> async::Task<>
{
    async::this_coroutine::setup_buffer_ring(128);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), port };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, /*reuse_port=*/true };

    if (auto ep = local_endpoint(acceptor))
        log::info("[server] listening on {}", *ep);

    Room room;

    while (true) {
        net::ip::tcp::endpoint peer;
        auto result = co_await acceptor.async_accept(peer);
        if (!result) {
            if (result.error() == std::errc::operation_canceled) co_return;
            log::error("[server] accept error: {}", result.error());
            continue;
        }
        async::co_spawn(session(std::move(*result), peer, room));
    }
}

auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    co_await signals.async_wait();
    log::info("[server] shutting down...");
    async::stop();
}

auto run(uint16_t port) -> async::Task<>
{
    async::co_spawn(shutdown_monitor());
    co_await server(port);
}

} // namespace

int main(int argc, char* argv[])
{
    uint16_t port = 9090;
    if (argc >= 2) {
        auto p = std::atoi(argv[1]);
        if (p > 0 && p < 65536)
            port = static_cast<uint16_t>(p);
    }
    async::run([port]{ return run(port); });
}
