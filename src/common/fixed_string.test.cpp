#include "fixed_string.h"

#include <catch2/catch_test_macros.hpp>

// ------- construction -------------------------------------------------------

TEST_CASE("FixedString: implicit construction from string literal", "[fixed_string]")
{
    constexpr FixedString s{ "hello" };
    REQUIRE(s.view() == "hello");
}

TEST_CASE("FixedString: capacity includes null terminator", "[fixed_string]")
{
    constexpr FixedString s{ "hello" };
    // "hello" has 5 chars + 1 null → capacity == 6
    STATIC_REQUIRE(decltype(s)::capacity == 6);
}

TEST_CASE("FixedString: view excludes null terminator", "[fixed_string]")
{
    constexpr FixedString s{ "hello" };
    REQUIRE(s.view().size() == 5);
}

TEST_CASE("FixedString: empty string literal", "[fixed_string]")
{
    constexpr FixedString s{ "" };
    REQUIRE(s.view().empty());
    STATIC_REQUIRE(decltype(s)::capacity == 1);
}

// ------- constexpr ----------------------------------------------------------

TEST_CASE("FixedString: usable as non-type template parameter", "[fixed_string]")
{
    // Compile-time check: the primary use case of FixedString
    []<FixedString S>() {
        REQUIRE(S.view() == "compile-time");
    }.template operator()<FixedString{ "compile-time" }>();
}

// ------- comparison ---------------------------------------------------------

TEST_CASE("FixedString: equality of identical values", "[fixed_string]")
{
    constexpr FixedString a{ "abc" };
    constexpr FixedString b{ "abc" };
    REQUIRE(a == b);
}

TEST_CASE("FixedString: inequality of different values", "[fixed_string]")
{
    constexpr FixedString a{ "abc" };
    constexpr FixedString b{ "xyz" };
    REQUIRE(a != b);
}

TEST_CASE("FixedString: spaceship ordering", "[fixed_string]")
{
    constexpr FixedString a{ "abc" };
    constexpr FixedString b{ "abd" };
    REQUIRE(a < b);
    REQUIRE(b > a);
}

// ------- view ---------------------------------------------------------------

TEST_CASE("FixedString: view aliases internal storage", "[fixed_string]")
{
    constexpr FixedString s{ "alias" };
    REQUIRE(s.view().data() == s.data.data());
}
