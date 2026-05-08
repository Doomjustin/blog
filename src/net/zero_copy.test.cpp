#include <net/zero_copy.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using net::ZeroCopyT;
using net::zero_copy;

TEST_CASE("zero_copy: wraps std::string", "[zero_copy]")
{
    const std::string s = "hello";
    auto zc = zero_copy(s);

    REQUIRE(zc.span.size() == s.size());
    REQUIRE(zc.span.data() == reinterpret_cast<const std::byte*>(s.data()));
}

TEST_CASE("zero_copy: wraps std::vector<std::byte>", "[zero_copy]")
{
    const std::vector<std::byte> v = { std::byte{1}, std::byte{2}, std::byte{3} };
    auto zc = zero_copy(v);

    REQUIRE(zc.span.size() == v.size());
    REQUIRE(zc.span.data() == v.data());
}

TEST_CASE("zero_copy: wraps std::array", "[zero_copy]")
{
    const std::array<char, 4> arr = { 'a', 'b', 'c', 'd' };
    auto zc = zero_copy(arr);

    REQUIRE(zc.span.size() == arr.size());
}

TEST_CASE("zero_copy: empty range", "[zero_copy]")
{
    const std::string empty;
    auto zc = zero_copy(empty);

    REQUIRE(zc.span.empty());
}

TEST_CASE("zero_copy: aliases original memory (no copy)", "[zero_copy]")
{
    const std::string s = "no copy";
    auto zc = zero_copy(s);

    REQUIRE(reinterpret_cast<const char*>(zc.span.data()) == s.data());
}
