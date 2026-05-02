#include "linger.h"

#include <catch2/catch_test_macros.hpp>

using net::LingerOption;

TEST_CASE("LingerOption: default construction", "[linger]")
{
    LingerOption opt{};
    REQUIRE(opt.value().l_onoff == 0);
    REQUIRE(opt.value().l_linger == 0);
    REQUIRE(opt.size() == sizeof(struct linger));
}

TEST_CASE("LingerOption: enabled with timeout", "[linger]")
{
    LingerOption opt{ true, 5 };
    REQUIRE(opt.value().l_onoff != 0);
    REQUIRE(opt.value().l_linger == 5);
}

TEST_CASE("LingerOption: disabled", "[linger]")
{
    LingerOption opt{ false, 10 };
    REQUIRE(opt.value().l_onoff == 0);
}

TEST_CASE("LingerOption: data() returns non-null pointer", "[linger]")
{
    LingerOption opt{ true, 1 };
    REQUIRE(opt.data() != nullptr);

    const LingerOption& copt = opt;
    REQUIRE(copt.data() != nullptr);
}

TEST_CASE("LingerOption: equality", "[linger]")
{
    LingerOption a{ true, 3 };
    LingerOption b{ true, 3 };
    LingerOption c{ true, 5 };
    LingerOption d{ false, 3 };

    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(a != d);
}

TEST_CASE("LingerOption: ordering", "[linger]")
{
    LingerOption off{ false, 0 };
    LingerOption on_short{ true, 1 };
    LingerOption on_long{ true, 10 };

    REQUIRE(off < on_short);
    REQUIRE(on_short < on_long);
}

TEST_CASE("LingerOption: socket option constants", "[linger]")
{
    REQUIRE(LingerOption::level == SOL_SOCKET);
    REQUIRE(LingerOption::name  == SO_LINGER);
}
