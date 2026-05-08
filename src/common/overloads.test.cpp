#include <common/overloads.h>

#include <variant>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Overload: single callable", "[overloads]")
{
    auto f = Overload{ [](int x) { return x * 2; } };
    REQUIRE(f(3) == 6);
}

TEST_CASE("Overload: multiple callables on std::variant", "[overloads]")
{
    using V = std::variant<int, float, std::string>;

    auto visitor = Overload{
        [](int v) { return std::string{ "int:" }    + std::to_string(v); },
        [](float v) { return std::string{ "float:" }  + std::to_string(v); },
        [](const std::string& v){ return std::string{ "string:" } + v; },
    };

    REQUIRE(std::visit(visitor, V{ 42 }) == "int:42");
    REQUIRE(std::visit(visitor, V{ 1.5F }) == "float:1.500000");
    REQUIRE(std::visit(visitor, V{ std::string{"hi"} }) == "string:hi");
}

TEST_CASE("Overload: CTAD deduction guide works", "[overloads]")
{
    // Verify CTAD: no explicit template args needed.
    Overload ov{ [](int) { return 1; }, [](double) { return 2; } };
    REQUIRE(ov(0)   == 1);
    REQUIRE(ov(0.0) == 2);
}
