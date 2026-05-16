#include "named_type.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Define some concrete named types for testing
using Meter = NamedType<double, "Meter", Arithmetic, Comparable>;
using Kilogram = NamedType<double, "Kilogram", Arithmetic, Comparable>;
using Seconds = NamedType<float, "Seconds", Arithmetic, Comparable>;
using Count = NamedType<int, "Count", Arithmetic, Comparable, Hashable, Bitwise>;

// ------- construction / get / operator* ------------------------------------

TEST_CASE("NamedType: construction and get", "[named_type]")
{
    constexpr Meter m{ 3.14 };
    REQUIRE(m.get() == 3.14);
    REQUIRE(*m == 3.14);
}

TEST_CASE("NamedType: get returns mutable reference", "[named_type]")
{
    Meter m{ 1.0 };
    m.get() = 2.0;
    REQUIRE(m.get() == 2.0);
}

// ------- Arithmetic skill --------------------------------------------------

TEST_CASE("NamedType Arithmetic: addition", "[named_type]")
{
    REQUIRE((Meter{ 1.0 } + Meter{ 2.0 }).get() == 3.0);
}

TEST_CASE("NamedType Arithmetic: subtraction", "[named_type]")
{
    REQUIRE((Meter{ 5.0 } - Meter{ 3.0 }).get() == 2.0);
}

TEST_CASE("NamedType Arithmetic: multiplication", "[named_type]")
{
    REQUIRE((Meter{ 2.0 } * Meter{ 3.0 }).get() == 6.0);
}

TEST_CASE("NamedType Arithmetic: division", "[named_type]")
{
    REQUIRE((Meter{ 6.0 } / Meter{ 2.0 }).get() == 3.0);
}

TEST_CASE("NamedType Arithmetic: compound assignment +=", "[named_type]")
{
    Meter m{ 1.0 };
    m += Meter{ 2.0 };
    REQUIRE(m.get() == 3.0);
}

TEST_CASE("NamedType Arithmetic: compound assignment -=", "[named_type]")
{
    Meter m{ 5.0 };
    m -= Meter{ 3.0 };
    REQUIRE(m.get() == 2.0);
}

TEST_CASE("NamedType Arithmetic: compound assignment *=", "[named_type]")
{
    Meter m{ 3.0 };
    m *= Meter{ 2.0 };
    REQUIRE(m.get() == 6.0);
}

TEST_CASE("NamedType Arithmetic: compound assignment /=", "[named_type]")
{
    Meter m{ 6.0 };
    m /= Meter{ 2.0 };
    REQUIRE(m.get() == 3.0);
}

TEST_CASE("NamedType Arithmetic: compound assignment %=  (integer)", "[named_type]")
{
    Count c{ 7 };
    c %= Count{ 3 };
    REQUIRE(c.get() == 1);
}

TEST_CASE("NamedType Arithmetic: remainder (floating-point via fmod)", "[named_type]")
{
    Seconds s{ 7.5F };
    s %= Seconds{ 3.0F };
    REQUIRE(s.get() == Catch::Approx(1.5F));
}

TEST_CASE("NamedType Arithmetic: prefix increment", "[named_type]")
{
    Count c{ 5 };
    REQUIRE((++c).get() == 6);
    REQUIRE(c.get() == 6);
}

TEST_CASE("NamedType Arithmetic: postfix increment", "[named_type]")
{
    Count c{ 5 };
    REQUIRE((c++).get() == 5);
    REQUIRE(c.get() == 6);
}

TEST_CASE("NamedType Arithmetic: prefix decrement", "[named_type]")
{
    Count c{ 5 };
    REQUIRE((--c).get() == 4);
}

TEST_CASE("NamedType Arithmetic: postfix decrement", "[named_type]")
{
    Count c{ 5 };
    REQUIRE((c--).get() == 5);
    REQUIRE(c.get() == 4);
}

TEST_CASE("NamedType Arithmetic: remainder (integer)", "[named_type]")
{
    REQUIRE((Count{ 7 } % Count{ 3 }).get() == 1);
}

// ------- Comparable skill --------------------------------------------------

TEST_CASE("NamedType Comparable: equality", "[named_type]")
{
    REQUIRE(Meter{ 1.0 } == Meter{ 1.0 });
    REQUIRE_FALSE(Meter{ 1.0 } == Meter{ 2.0 });
}

TEST_CASE("NamedType Comparable: spaceship ordering", "[named_type]")
{
    REQUIRE(Meter{ 1.0 } < Meter{ 2.0 });
    REQUIRE(Meter{ 2.0 } > Meter{ 1.0 });
    REQUIRE(Meter{ 1.0 } <= Meter{ 1.0 });
    REQUIRE(Meter{ 1.0 } >= Meter{ 1.0 });
    REQUIRE(Meter{ 2.0 } >= Meter{ 1.0 });
}

// ------- Bitwise skill -----------------------------------------------------

TEST_CASE("NamedType Bitwise: AND", "[named_type]")
{
    REQUIRE((Count{ 0b1010 } & Count{ 0b1100 }).get() == 0b1000);
}

TEST_CASE("NamedType Bitwise: OR", "[named_type]")
{
    REQUIRE((Count{ 0b1010 } | Count{ 0b0101 }).get() == 0b1111);
}

TEST_CASE("NamedType Bitwise: XOR", "[named_type]")
{
    REQUIRE((Count{ 0b1010 } ^ Count{ 0b1100 }).get() == 0b0110);
}

TEST_CASE("NamedType Bitwise: compound assignment &=", "[named_type]")
{
    Count c{ 0b1010 };
    c &= Count{ 0b1100 };
    REQUIRE(c.get() == 0b1000);
}

TEST_CASE("NamedType Bitwise: compound assignment |=", "[named_type]")
{
    Count c{ 0b1010 };
    c |= Count{ 0b0101 };
    REQUIRE(c.get() == 0b1111);
}

TEST_CASE("NamedType Bitwise: compound assignment ^=", "[named_type]")
{
    Count c{ 0b1010 };
    c ^= Count{ 0b1100 };
    REQUIRE(c.get() == 0b0110);
}

// ------- Hashable skill ----------------------------------------------------

TEST_CASE("NamedType Hashable: same value produces same hash", "[named_type]")
{
    REQUIRE(Count{ 42 }.hash() == Count{ 42 }.hash());
}

TEST_CASE("NamedType Hashable: different values produce different hashes", "[named_type]")
{
    REQUIRE(Count{ 1 }.hash() != Count{ 2 }.hash());
}
