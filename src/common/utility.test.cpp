#include "utility.h"

#include <numbers>

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

// ------- numeric_cast -------------------------------------------------------

TEST_CASE("numeric_cast<int>: valid decimal string", "[numeric_cast]")
{
    auto result = numeric_cast<int>("42");
    REQUIRE(result.has_value());
    REQUIRE(*result == 42);
}

TEST_CASE("numeric_cast<int>: negative value", "[numeric_cast]")
{
    auto result = numeric_cast<int>("-100");
    REQUIRE(result.has_value());
    REQUIRE(*result == -100);
}

TEST_CASE("numeric_cast<int>: zero", "[numeric_cast]")
{
    auto result = numeric_cast<int>("0");
    REQUIRE(result.has_value());
    REQUIRE(*result == 0);
}

TEST_CASE("numeric_cast<int>: non-numeric string returns error", "[numeric_cast]")
{
    auto result = numeric_cast<int>("abc");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("numeric_cast<int>: trailing non-numeric characters returns error", "[numeric_cast]")
{
    // "42x" — from_chars stops at 'x', ptr != end → invalid_argument
    auto result = numeric_cast<int>("42x");
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == std::make_error_code(std::errc::invalid_argument));
}

TEST_CASE("numeric_cast<int>: empty string returns error", "[numeric_cast]")
{
    auto result = numeric_cast<int>("");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("numeric_cast<unsigned>: valid unsigned value", "[numeric_cast]")
{
    auto result = numeric_cast<unsigned>("255");
    REQUIRE(result.has_value());
    REQUIRE(*result == 255U);
}

TEST_CASE("numeric_cast<uint16_t>: overflow returns error", "[numeric_cast]")
{
    auto result = numeric_cast<uint16_t>("70000");
    REQUIRE_FALSE(result.has_value());
}

// ------- string_cast --------------------------------------------------------

TEST_CASE("string_cast<float>: round-trips via numeric_cast", "[string_cast]")
{
    const float original = 3.14F;
    auto str = string_cast<float>(original);
    REQUIRE(str.has_value());
    auto back = numeric_cast<float>(*str);
    REQUIRE(back.has_value());
    REQUIRE(*back == original);
}

TEST_CASE("string_cast<double>: round-trips via numeric_cast", "[string_cast]")
{
    const double original = std::numbers::e;
    auto str = string_cast<double>(original);
    REQUIRE(str.has_value());
    auto back = numeric_cast<double>(*str);
    REQUIRE(back.has_value());
    REQUIRE(*back == original);
}

TEST_CASE("string_cast<double>: zero", "[string_cast]")
{
    auto str = string_cast<double>(0.0);
    REQUIRE(str.has_value());
    REQUIRE(*str == "0");
}
