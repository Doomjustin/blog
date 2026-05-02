#include "utility.h"

#include <catch2/catch_test_macros.hpp>

// ------- to_uppercase -------------------------------------------------------

TEST_CASE("to_uppercase: all lowercase letters", "[strings]")
{
    REQUIRE(to_uppercase("hello") == "HELLO");
}

TEST_CASE("to_uppercase: already uppercase is unchanged", "[strings]")
{
    REQUIRE(to_uppercase("HELLO") == "HELLO");
}

TEST_CASE("to_uppercase: mixed case", "[strings]")
{
    REQUIRE(to_uppercase("Hello World") == "HELLO WORLD");
}

TEST_CASE("to_uppercase: non-alpha characters are unchanged", "[strings]")
{
    REQUIRE(to_uppercase("abc123!@#") == "ABC123!@#");
}

TEST_CASE("to_uppercase: empty string", "[strings]")
{
    REQUIRE(to_uppercase("") == "");
}

TEST_CASE("to_uppercase: returns independent copy", "[strings]")
{
    std::string src = "hello";
    auto result = to_uppercase(src);
    REQUIRE(result == "HELLO");
    REQUIRE(src == "hello");
}

// ------- to_lowercase -------------------------------------------------------

TEST_CASE("to_lowercase: all uppercase letters", "[strings]")
{
    REQUIRE(to_lowercase("HELLO") == "hello");
}

TEST_CASE("to_lowercase: already lowercase is unchanged", "[strings]")
{
    REQUIRE(to_lowercase("hello") == "hello");
}

TEST_CASE("to_lowercase: mixed case", "[strings]")
{
    REQUIRE(to_lowercase("Hello World") == "hello world");
}

TEST_CASE("to_lowercase: non-alpha characters are unchanged", "[strings]")
{
    REQUIRE(to_lowercase("ABC123!@#") == "abc123!@#");
}

TEST_CASE("to_lowercase: empty string", "[strings]")
{
    REQUIRE(to_lowercase("") == "");
}

TEST_CASE("to_lowercase: returns independent copy", "[strings]")
{
    std::string src = "HELLO";
    auto result = to_lowercase(src);
    REQUIRE(result == "hello");
    REQUIRE(src == "HELLO");
}

// ------- round-trip ---------------------------------------------------------

TEST_CASE("to_lowercase(to_uppercase(s)) == to_lowercase(s)", "[strings]")
{
    const std::string input = "Hello World 123";
    REQUIRE(to_lowercase(to_uppercase(input)) == to_lowercase(input));
}
