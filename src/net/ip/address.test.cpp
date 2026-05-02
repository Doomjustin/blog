#include "ip/address.h"

#include <catch2/catch_test_macros.hpp>

using namespace net::ip;

// ─── AddressV4 ───────────────────────────────────────────────────────────────

TEST_CASE("AddressV4: factory constants", "[address][v4]")
{
    SECTION("any() is 0.0.0.0")
    {
        REQUIRE(AddressV4::any().to_string() == "0.0.0.0");
    }

    SECTION("loopback() is 127.0.0.1")
    {
        REQUIRE(AddressV4::loopback().to_string() == "127.0.0.1");
    }

    SECTION("broadcast() is 255.255.255.255")
    {
        REQUIRE(AddressV4::broadcast().to_string() == "255.255.255.255");
    }
}

TEST_CASE("AddressV4: from_string round-trip", "[address][v4]")
{
    SECTION("loopback string")
    {
        auto addr = AddressV4::from_string("127.0.0.1");
        REQUIRE(addr.to_string() == "127.0.0.1");
    }

    SECTION("arbitrary address")
    {
        auto addr = AddressV4::from_string("192.168.1.100");
        REQUIRE(addr.to_string() == "192.168.1.100");
    }

    SECTION("invalid string throws")
    {
        REQUIRE_THROWS_AS(AddressV4::from_string("not_an_ip"), std::system_error);
    }
}

TEST_CASE("AddressV4: equality and ordering", "[address][v4]")
{
    auto a = AddressV4::from_string("10.0.0.1");
    auto b = AddressV4::from_string("10.0.0.2");
    auto c = AddressV4::from_string("10.0.0.1");

    REQUIRE(a == c);
    REQUIRE(a != b);
    REQUIRE(a < b);
    REQUIRE(b > a);
}

// ─── AddressV6 ───────────────────────────────────────────────────────────────

TEST_CASE("AddressV6: factory constants", "[address][v6]")
{
    SECTION("any() is ::")
    {
        REQUIRE(AddressV6::any().to_string() == "::");
    }

    SECTION("loopback() is ::1")
    {
        REQUIRE(AddressV6::loopback().to_string() == "::1");
    }
}

TEST_CASE("AddressV6: from_string round-trip", "[address][v6]")
{
    SECTION("loopback string")
    {
        auto addr = AddressV6::from_string("::1");
        REQUIRE(addr.to_string() == "::1");
    }

    SECTION("invalid string throws")
    {
        REQUIRE_THROWS_AS(AddressV6::from_string("not_an_ip"), std::system_error);
    }
}

TEST_CASE("AddressV6: equality", "[address][v6]")
{
    auto a = AddressV6::loopback();
    auto b = AddressV6::loopback();
    auto c = AddressV6::any();

    REQUIRE(a == b);
    REQUIRE(a != c);
}

// ─── Address (type-erased) ────────────────────────────────────────────────────

TEST_CASE("Address: holds v4", "[address][generic]")
{
    Address addr{ AddressV4::loopback() };

    REQUIRE(addr.is_v4());
    REQUIRE_FALSE(addr.is_v6());
    REQUIRE(addr.to_string() == "127.0.0.1");
    REQUIRE(addr.to_v4().to_string() == "127.0.0.1");
}

TEST_CASE("Address: holds v6", "[address][generic]")
{
    Address addr{ AddressV6::loopback() };

    REQUIRE(addr.is_v6());
    REQUIRE_FALSE(addr.is_v4());
    REQUIRE(addr.to_string() == "::1");
}

TEST_CASE("Address: from_string", "[address][generic]")
{
    SECTION("parses v4")
    {
        auto addr = Address::from_string("10.0.0.1");
        REQUIRE(addr.is_v4());
        REQUIRE(addr.to_string() == "10.0.0.1");
    }

    SECTION("parses v6")
    {
        auto addr = Address::from_string("::1");
        REQUIRE(addr.is_v6());
        REQUIRE(addr.to_string() == "::1");
    }

    SECTION("invalid string throws")
    {
        REQUIRE_THROWS(Address::from_string("garbage"));
    }
}

TEST_CASE("Address: equality", "[address][generic]")
{
    Address a{ AddressV4::loopback() };
    Address b{ AddressV4::loopback() };
    Address c{ AddressV6::loopback() };

    REQUIRE(a == b);
    REQUIRE(a != c);
}
