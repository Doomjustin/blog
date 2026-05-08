#include <net/option.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include <catch2/catch_test_macros.hpp>

using net::BooleanOption;
using net::ValueOption;

// ─── BooleanOption ────────────────────────────────────────────────────────────

using ReuseAddr = BooleanOption<SOL_SOCKET, SO_REUSEADDR>;

TEST_CASE("BooleanOption: default construction is false", "[option]")
{
    ReuseAddr opt{};
    REQUIRE_FALSE(opt.value());
    REQUIRE_FALSE(static_cast<bool>(opt));
}

TEST_CASE("BooleanOption: explicit true", "[option]")
{
    ReuseAddr opt{ true };
    REQUIRE(opt.value());
    REQUIRE(static_cast<bool>(opt));
}

TEST_CASE("BooleanOption: explicit false", "[option]")
{
    ReuseAddr opt{ false };
    REQUIRE_FALSE(opt.value());
}

TEST_CASE("BooleanOption: data() and size()", "[option]")
{
    ReuseAddr opt{ true };
    REQUIRE(opt.data() != nullptr);
    REQUIRE(opt.size() == sizeof(int));

    const ReuseAddr& copt = opt;
    REQUIRE(copt.data() != nullptr);
}

TEST_CASE("BooleanOption: level and name constants", "[option]")
{
    REQUIRE(ReuseAddr::level == SOL_SOCKET);
    REQUIRE(ReuseAddr::name  == SO_REUSEADDR);
}

TEST_CASE("BooleanOption: equality and ordering", "[option]")
{
    ReuseAddr t{ true };
    ReuseAddr f{ false };

    REQUIRE(t == ReuseAddr{ true });
    REQUIRE(f == ReuseAddr{ false });
    REQUIRE(t != f);
    REQUIRE(f < t);
}

// ─── ValueOption ─────────────────────────────────────────────────────────────

using RecvBuf = ValueOption<SOL_SOCKET, SO_RCVBUF>;

TEST_CASE("ValueOption: default construction is zero", "[option]")
{
    RecvBuf opt{};
    REQUIRE(opt.value() == 0);
}

TEST_CASE("ValueOption: explicit value", "[option]")
{
    RecvBuf opt{ 65536 };
    REQUIRE(opt.value() == 65536);
}

TEST_CASE("ValueOption: data() and size()", "[option]")
{
    RecvBuf opt{ 4096 };
    REQUIRE(opt.data() != nullptr);
    REQUIRE(opt.size() == sizeof(int));

    const RecvBuf& copt = opt;
    REQUIRE(copt.data() != nullptr);
}

TEST_CASE("ValueOption: level and name constants", "[option]")
{
    REQUIRE(RecvBuf::level == SOL_SOCKET);
    REQUIRE(RecvBuf::name  == SO_RCVBUF);
}

TEST_CASE("ValueOption: equality", "[option]")
{
    RecvBuf a{ 1024 };
    RecvBuf b{ 1024 };
    RecvBuf c{ 2048 };

    REQUIRE(a == b);
    REQUIRE(a != c);
    REQUIRE(a < c);
}
