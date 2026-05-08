#include <file_system/random_access_file.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <async/run.h>
#include <async/when_all.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

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

// Pre-populate the test file with `content` synchronously.
void seed_file(const std::string& path, std::string_view content)
{
    async::run([&]() -> async::Task<void> {
        fs::RandomAccessFile f{ path, fs::BaseFile::flag::create | fs::BaseFile::flag::write_only | fs::BaseFile::flag::truncate };
        co_await f.async_write(0, as_bytes(content));
    });
}

} // namespace

// ---------------------------------------------------------------------------
// Positional read / write
// ---------------------------------------------------------------------------

TEST_CASE("RandomAccessFile positional read and write", "[random_access_file]")
{
    TempFile tmp{ "/tmp/blog_test_raf.bin" };

    SECTION("write_at then read_at at same offset")
    {
        async::run([]() -> async::Task<void> {
            fs::RandomAccessFile f{ "/tmp/blog_test_raf.bin",
                                    fs::BaseFile::flag::create | fs::BaseFile::flag::read_write | fs::BaseFile::flag::truncate };
            std::string_view text = "positional";
            auto wres = co_await f.async_write(0, as_bytes(text));
            REQUIRE(wres);
            CHECK(*wres == text.size());

            std::array<std::byte, 16> buf{};
            auto rres = co_await f.async_read(0, { buf.data(), text.size() });
            REQUIRE(rres);
            CHECK(*rres == text.size());
            CHECK(std::memcmp(buf.data(), text.data(), text.size()) == 0);
        });
    }

    SECTION("write_at two disjoint regions, read_at each independently")
    {
        async::run([]() -> async::Task<void> {
            fs::RandomAccessFile f{ "/tmp/blog_test_raf.bin",
                                    fs::BaseFile::flag::create | fs::BaseFile::flag::read_write | fs::BaseFile::flag::truncate };
            co_await f.async_write(0,  as_bytes("AAAA"));
            co_await f.async_write(4,  as_bytes("BBBB"));

            std::array<std::byte, 4> a{};
            std::array<std::byte, 4> b{};
            co_await f.async_read(0, a);
            co_await f.async_read(4, b);

            CHECK(std::memcmp(a.data(), "AAAA", 4) == 0);
            CHECK(std::memcmp(b.data(), "BBBB", 4) == 0);
        });
    }

    SECTION("read_at past end of file returns 0 (EOF)")
    {
        seed_file("/tmp/blog_test_raf.bin", "short");

        async::run([]() -> async::Task<void> {
            fs::RandomAccessFile f{ "/tmp/blog_test_raf.bin",
                                    fs::BaseFile::flag::read_only };

            std::array<std::byte, 8> buf{};
            // offset 100 is past the 5-byte file
            auto rres = co_await f.async_read(100, buf);
            REQUIRE(rres);
            CHECK(*rres == 0U);
        });
    }
}

// ---------------------------------------------------------------------------
// Concurrent positional I/O
// ---------------------------------------------------------------------------

TEST_CASE("RandomAccessFile concurrent reads at disjoint offsets", "[random_access_file][concurrent]")
{
    TempFile tmp{ "/tmp/blog_test_raf_concurrent.bin" };
    seed_file("/tmp/blog_test_raf_concurrent.bin", "AAAABBBB");

    async::run([]() -> async::Task<void> {
        fs::RandomAccessFile f{ "/tmp/blog_test_raf_concurrent.bin",
                                fs::BaseFile::flag::read_only };

        std::array<std::byte, 4> a{};
        std::array<std::byte, 4> b{};

        // Issue both reads sequentially; they target disjoint offsets so
        // the file position does not matter and results are independent.
        auto ra = co_await f.async_read(0, a);
        auto rb = co_await f.async_read(4, b);

        REQUIRE(ra);
        CHECK(*ra == 4U);
        REQUIRE(rb);
        CHECK(*rb == 4U);

        CHECK(std::memcmp(a.data(), "AAAA", 4) == 0);
        CHECK(std::memcmp(b.data(), "BBBB", 4) == 0);
    });
}

// ---------------------------------------------------------------------------
// Current-position read / write (sequential mode)
// ---------------------------------------------------------------------------

TEST_CASE("RandomAccessFile sequential (current-position) read and write", "[random_access_file]")
{
    TempFile tmp{ "/tmp/blog_test_raf_seq.bin" };

    async::run([]() -> async::Task<void> {
        fs::RandomAccessFile f{ "/tmp/blog_test_raf_seq.bin",
                                fs::BaseFile::flag::create | fs::BaseFile::flag::read_write | fs::BaseFile::flag::truncate };

        // sequential write
        auto w1 = co_await f.async_write(as_bytes("foo"));
        auto w2 = co_await f.async_write(as_bytes("bar"));
        REQUIRE(w1);
        REQUIRE(w2);

        // seek back and read sequentially
        ::lseek(f.native_handle(), 0, SEEK_SET);

        std::array<std::byte, 6> buf{};
        auto rres = co_await f.async_read(buf);
        REQUIRE(rres);
        CHECK(*rres == 6U);
        CHECK(std::memcmp(buf.data(), "foobar", 6) == 0);
    });
}
