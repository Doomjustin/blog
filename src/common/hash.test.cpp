#include "hash.h"

#include <string>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

// ------- use_std_hash -------------------------------------------------------

TEST_CASE("use_std_hash: same input produces same result", "[hash]")
{
    REQUIRE(hash(use_std_hash("hello")) == hash(use_std_hash("hello")));
}

TEST_CASE("use_std_hash: different inputs produce different results", "[hash]")
{
    REQUIRE(hash(use_std_hash("foo")) != hash(use_std_hash("bar")));
}

TEST_CASE("use_std_hash: empty string is stable", "[hash]")
{
    REQUIRE(hash(use_std_hash("")) == hash(use_std_hash("")));
}

// ------- use_fnv_1a ---------------------------------------------------------

TEST_CASE("use_fnv_1a: known value for empty string", "[hash]")
{
    // FNV-1a of empty input equals the offset basis by definition
    constexpr auto expected = 14695981039346656037ULL;
    REQUIRE(hash(use_fnv_1a("")) == expected);
}

TEST_CASE("use_fnv_1a: known value for 'a'", "[hash]")
{
    // FNV-1a("a") = (offset_basis ^ 0x61) * prime
    constexpr auto expected = (14695981039346656037ULL ^ 0x61ULL) * 1099511628211ULL;
    REQUIRE(hash(use_fnv_1a("a")) == expected);
}

TEST_CASE("use_fnv_1a: same input is idempotent", "[hash]")
{
    REQUIRE(hash(use_fnv_1a("hello world")) == hash(use_fnv_1a("hello world")));
}

TEST_CASE("use_fnv_1a: different inputs produce different results", "[hash]")
{
    REQUIRE(hash(use_fnv_1a("foo")) != hash(use_fnv_1a("bar")));
}

TEST_CASE("use_fnv_1a: is constexpr", "[hash]")
{
    constexpr auto h = hash(use_fnv_1a("compile-time"));
    REQUIRE(h != 0);
}

// ------- StringHash ---------------------------------------------------------

TEST_CASE("StringHash: heterogeneous lookup in unordered_map", "[hash]")
{
    std::unordered_map<std::string, int, StringHash, std::equal_to<>> map;
    map["hello"] = 42;

    // Lookup with std::string_view must find the entry without constructing a key
    REQUIRE(map.contains(std::string_view{ "hello" }));
    REQUIRE_FALSE(map.contains(std::string_view{ "world" }));
}

TEST_CASE("StringHash: consistent with FNV-1a", "[hash]")
{
    StringHash h{};
    auto sv = std::string_view{ "test" };
    REQUIRE(h(sv) == static_cast<std::size_t>(hash(use_fnv_1a(sv))));
}

// ------- PmrStringHash ------------------------------------------------------

TEST_CASE("PmrStringHash: heterogeneous lookup in unordered_map", "[hash]")
{
    std::unordered_map<std::string, int, PmrStringHash, std::equal_to<>> map;
    map["key"] = 99;

    REQUIRE(map.contains(std::string_view{ "key" }));
    REQUIRE_FALSE(map.contains(std::string_view{ "missing" }));
}

TEST_CASE("PmrStringHash: produces same value as StringHash for same input", "[hash]")
{
    StringHash sh{};
    PmrStringHash ph{};
    const auto sv = std::string_view{ "same-input" };
    REQUIRE(sh(sv) == ph(sv));
}
