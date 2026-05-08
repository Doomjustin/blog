#include <file_system/stream_file.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <async/run.h>
#include <file_system/async_open.h>
#include <file_system/transfer.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// RAII wrapper that deletes a temporary file on destruction.
struct TempFile {
    explicit TempFile(std::string path) : path_{ std::move(path) } {}
    ~TempFile() { std::filesystem::remove(path_); }

    TempFile(const TempFile&) = delete;
    auto operator=(const TempFile&) -> TempFile& = delete;

    const std::string path_;
};

auto as_bytes(std::string_view sv) -> std::span<const std::byte>
{
    return { reinterpret_cast<const std::byte*>(sv.data()), sv.size() };
}

} // namespace

// ---------------------------------------------------------------------------
// StreamFile: synchronous open / seek
// ---------------------------------------------------------------------------

TEST_CASE("StreamFile synchronous open and seek", "[stream_file]")
{
    TempFile tmp{ "/tmp/blog_test_stream_sync.txt" };

    SECTION("open for write then read back")
    {
        async::run([]() -> async::Task<void> {
            // write
            {
                fs::StreamFile f{ "/tmp/blog_test_stream_sync.txt",
                                  fs::BaseFile::flag::create | fs::BaseFile::flag::write_only | fs::BaseFile::flag::truncate };
                std::string_view text = "hello";
                auto wres = co_await f.async_write(as_bytes(text));
                REQUIRE(wres);
                CHECK(*wres == text.size());
            }
            // read back
            {
                fs::StreamFile f{ "/tmp/blog_test_stream_sync.txt",
                                  fs::BaseFile::flag::read_only };
                std::array<std::byte, 16> buf{};
                auto rres = co_await f.async_read(buf);
                REQUIRE(rres);
                CHECK(*rres == 5U);
                CHECK(std::memcmp(buf.data(), "hello", 5) == 0);
            }
        });
    }

    SECTION("seek to beginning and re-read")
    {
        async::run([]() -> async::Task<void> {
            fs::StreamFile f{ "/tmp/blog_test_stream_sync.txt",
                              fs::StreamFile::flag::create | fs::StreamFile::flag::read_write | fs::StreamFile::flag::truncate };

            std::string_view text = "world";
            co_await f.async_write(as_bytes(text));

            auto sres = f.seek(0, fs::StreamFile::how::seek_set);
            REQUIRE(sres);
            CHECK(*sres == 0);

            std::array<std::byte, 8> buf{};
            auto rres = co_await f.async_read(buf);
            REQUIRE(rres);
            CHECK(*rres == 5U);
            CHECK(std::memcmp(buf.data(), "world", 5) == 0);
        });
    }
}

// ---------------------------------------------------------------------------
// StreamFile: async open + async close
// ---------------------------------------------------------------------------

TEST_CASE("StreamFile async_open and async_close", "[stream_file][async_open]")
{
    TempFile tmp{ "/tmp/blog_test_stream_async_open.txt" };

    SECTION("async_open creates file and async_close releases fd")
    {
        async::run([]() -> async::Task<void> {
            auto& ctx = async::this_coroutine::context();

            auto f = co_await fs::async_open<fs::StreamFile>(
                ctx,
                "/tmp/blog_test_stream_async_open.txt",
                fs::StreamFile::flag::create | fs::StreamFile::flag::write_only | fs::StreamFile::flag::truncate);

            std::string_view text = "async";
            auto wres = co_await f.async_write(as_bytes(text));
            REQUIRE(wres);

            auto cres = co_await f.async_close();
            CHECK(cres.has_value());
        });
    }
}

// ---------------------------------------------------------------------------
// StreamFile: fsync / fdatasync
// ---------------------------------------------------------------------------

TEST_CASE("StreamFile async_fsync and async_fdatasync", "[stream_file][fsync]")
{
    TempFile tmp{ "/tmp/blog_test_stream_fsync.txt" };

    async::run([]() -> async::Task<void> {
        fs::StreamFile f{ "/tmp/blog_test_stream_fsync.txt",
                          fs::StreamFile::flag::create | fs::StreamFile::flag::write_only | fs::StreamFile::flag::truncate };

        std::string_view text = "durable";
        co_await f.async_write(as_bytes(text));

        auto fsync_res = co_await f.async_fsync();
        CHECK(fsync_res.has_value());

        co_await f.async_write(as_bytes(text));

        auto fdata_res = co_await f.async_fdatasync();
        CHECK(fdata_res.has_value());
    });
}

// ---------------------------------------------------------------------------
// fs::read / fs::write (transfer helpers)
// ---------------------------------------------------------------------------

TEST_CASE("fs::read and fs::write fill / drain entire buffer", "[transfer]")
{
    TempFile tmp{ "/tmp/blog_test_transfer.txt" };

    SECTION("write_all then read_all round-trip")
    {
        async::run([]() -> async::Task<void> {
            constexpr std::string_view payload = "round-trip payload";

            // write_all
            {
                fs::StreamFile f{ "/tmp/blog_test_transfer.txt",
                                  fs::StreamFile::flag::create | fs::StreamFile::flag::write_only | fs::StreamFile::flag::truncate };
                auto wres = co_await fs::write(f, as_bytes(payload));
                REQUIRE(wres);
                CHECK(*wres == payload.size());
            }

            // read_all fills the exact buffer
            {
                fs::StreamFile f{ "/tmp/blog_test_transfer.txt",
                                  fs::StreamFile::flag::read_only };
                std::array<std::byte, payload.size()> buf{};
                auto rres = co_await fs::read(f, buf);
                REQUIRE(rres);
                CHECK(*rres == payload.size());
                CHECK(std::memcmp(buf.data(), payload.data(), payload.size()) == 0);
            }
        });
    }

    SECTION("read_all on EOF returns bytes read, not an error")
    {
        async::run([]() -> async::Task<void> {
            // File has 3 bytes, buffer is 8 — should return 3, not error
            {
                fs::StreamFile w{ "/tmp/blog_test_transfer.txt",
                                  fs::StreamFile::flag::create | fs::StreamFile::flag::write_only | fs::StreamFile::flag::truncate };
                co_await fs::write(w, as_bytes("abc"));
            }
            {
                fs::StreamFile f{ "/tmp/blog_test_transfer.txt",
                                  fs::StreamFile::flag::read_only };
                std::array<std::byte, 8> buf{};
                auto rres = co_await fs::read(f, buf);
                REQUIRE(rres);
                CHECK(*rres == 3U);
            }
        });
    }
}
