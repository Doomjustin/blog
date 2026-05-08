#include <common/as_string.h>

#include <array>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("as_string: const span", "[as_string]")
{
    SECTION("normal ASCII bytes")
    {
        const std::string src = "hello";
        auto bytes = std::as_bytes(std::span{ src });
        auto sv = as_string(bytes);
        REQUIRE(sv == "hello");
        REQUIRE(sv.data() == src.data());
        REQUIRE(sv.size() == src.size());
    }

    SECTION("empty span")
    {
        std::span<const std::byte> empty{};
        auto sv = as_string(empty);
        REQUIRE(sv.empty());
        REQUIRE(sv.size() == 0);
    }

    SECTION("binary bytes with embedded null")
    {
        const std::array<std::byte, 3> data = {
            std::byte{'a'}, std::byte{0}, std::byte{'b'}
        };
        auto sv = as_string(std::span{ data });
        REQUIRE(sv.size() == 3);
        REQUIRE(sv[0] == 'a');
        REQUIRE(sv[1] == '\0');
        REQUIRE(sv[2] == 'b');
    }

    SECTION("returned view aliases original memory")
    {
        std::string src = "alias";
        auto bytes = std::as_bytes(std::span{ src });
        auto sv = as_string(bytes);
        REQUIRE(sv.data() == src.data());
    }
}

TEST_CASE("as_string: mutable span", "[as_string]")
{
    SECTION("normal ASCII bytes")
    {
        std::string src = "world";
        auto bytes = std::as_writable_bytes(std::span{ src });
        auto sv = as_string(bytes);
        REQUIRE(sv == "world");
        REQUIRE(sv.data() == src.data());
    }

    SECTION("empty mutable span")
    {
        std::span<std::byte> empty{};
        auto sv = as_string(empty);
        REQUIRE(sv.empty());
    }

    SECTION("returned view is read-only string_view over mutable memory")
    {
        std::string src = "mutable";
        auto bytes = std::as_writable_bytes(std::span{ src });
        auto sv = as_string(bytes);
        // View aliases the same memory; modifying src invalidates the view
        // — just check address equality here.
        REQUIRE(sv.data() == src.data());
        REQUIRE(sv.size() == src.size());
    }
}
