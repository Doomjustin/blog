#include "endpoint.h"

#include <catch2/catch_test_macros.hpp>

#include <net/ip/address.h>
#include <net/ip/tcp.h>

using namespace net::ip;
using Endpoint = tcp::endpoint;

TEST_CASE("BasicEndpoint: default construction", "[endpoint]")
{
    Endpoint ep{};

    REQUIRE(ep.port() == 0);
    REQUIRE(ep.address().is_v4());
    REQUIRE(ep.size() == sizeof(sockaddr_in));
}

TEST_CASE("BasicEndpoint: construct from address and port", "[endpoint]")
{
    SECTION("IPv4 loopback")
    {
        Endpoint ep{ AddressV4::loopback(), 8080 };

        REQUIRE(ep.port() == 8080);
        REQUIRE(ep.address().is_v4());
        REQUIRE(ep.address().to_string() == "127.0.0.1");
        REQUIRE(ep.size() == sizeof(sockaddr_in));
    }

    SECTION("IPv6 loopback")
    {
        Endpoint ep{ AddressV6::loopback(), 9090 };

        REQUIRE(ep.port() == 9090);
        REQUIRE(ep.address().is_v6());
        REQUIRE(ep.address().to_string() == "::1");
        REQUIRE(ep.size() == sizeof(sockaddr_in6));
    }

    SECTION("port zero")
    {
        Endpoint ep{ AddressV4::any(), 0 };
        REQUIRE(ep.port() == 0);
    }

    SECTION("max port 65535")
    {
        Endpoint ep{ AddressV4::any(), 65535 };
        REQUIRE(ep.port() == 65535);
    }
}

TEST_CASE("BasicEndpoint: construct from protocol and port", "[endpoint]")
{
    SECTION("v4 any-address")
    {
        Endpoint ep{ tcp::v4(), 1234 };

        REQUIRE(ep.port() == 1234);
        REQUIRE(ep.address().is_v4());
        REQUIRE(ep.address().to_string() == "0.0.0.0");
    }

    SECTION("v6 any-address")
    {
        Endpoint ep{ tcp::v6(), 1234 };

        REQUIRE(ep.port() == 1234);
        REQUIRE(ep.address().is_v6());
        REQUIRE(ep.address().to_string() == "::");
    }
}

TEST_CASE("BasicEndpoint: from_string", "[endpoint]")
{
    SECTION("IPv4")
    {
        auto ep = Endpoint::from_string("192.168.0.1", 443);
        REQUIRE(ep.port() == 443);
        REQUIRE(ep.address().to_string() == "192.168.0.1");
    }

    SECTION("IPv6")
    {
        auto ep = Endpoint::from_string("::1", 443);
        REQUIRE(ep.port() == 443);
        REQUIRE(ep.address().to_string() == "::1");
    }
}

TEST_CASE("BasicEndpoint: capacity and size", "[endpoint]")
{
    Endpoint v4ep{ AddressV4::loopback(), 80 };
    Endpoint v6ep{ AddressV6::loopback(), 80 };

    REQUIRE(v4ep.capacity() == sizeof(sockaddr_storage));
    REQUIRE(v6ep.capacity() == sizeof(sockaddr_storage));
    REQUIRE(v4ep.size() == sizeof(sockaddr_in));
    REQUIRE(v6ep.size() == sizeof(sockaddr_in6));
}

TEST_CASE("BasicEndpoint: protocol() reflects family", "[endpoint]")
{
    REQUIRE(Endpoint{ tcp::v4(), 0 }.protocol().domain() == AF_INET);
    REQUIRE(Endpoint{ tcp::v6(), 0 }.protocol().domain() == AF_INET6);
}
