#include "dict.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("meta::Dict 支持按 symbol key 存取值", "[utility][meta][dict]")
{
    using namespace meta::literals;

    const auto dict = meta::Dict{
        "name"_sym = "xin",
        "count"_sym = 7,
    };

    STATIC_REQUIRE(std::same_as<decltype(dict.key<"name">()), std::string_view>);

    SECTION("可以读取 string 类型字段")
    {
        const auto name = dict.key<"name">();
        REQUIRE(name == "xin");
    }

    SECTION("可以读取整数类型字段")
    {
        const auto count = dict.key<"count">();
        REQUIRE(count == 7);
    }
}

TEST_CASE("meta::Dict 支持 contains 编译期检查", "[utility][meta][dict]")
{
    using namespace meta::literals;

    constexpr auto dict = meta::Dict{
        "name"_sym = std::string_view{ "xin" },
        "count"_sym = 7,
    };

    constexpr auto has_name = dict.contains<"name">();
    constexpr auto has_count = dict.contains<"count">();
    constexpr auto has_missing = dict.contains<"missing">();

    STATIC_REQUIRE(has_name);
    STATIC_REQUIRE(has_count);
    STATIC_REQUIRE_FALSE(has_missing);
}

TEST_CASE("meta::Dict 支持从 int 数组构造字段", "[utility][meta][dict]")
{
    using namespace meta::literals;

    int values[] = { 1, 2, 3 };

    const auto dict = meta::Dict{
        "values"_sym = values,
    };

    auto stored = dict.key<"values">();
    STATIC_REQUIRE(std::same_as<decltype(stored), std::array<int, 3>>);
    REQUIRE(stored[0] == 1);
    REQUIRE(stored[1] == 2);
    REQUIRE(stored[2] == 3);
}

TEST_CASE("meta::Dict 支持从可变 char 数组推导 string_view", "[utility][meta][dict]")
{
    using namespace meta::literals;

    char text[] = "xin";

    const auto dict = meta::Dict{
        "name"_sym = text,
    };

    auto name = dict.key<"name">();
    STATIC_REQUIRE(std::same_as<decltype(name), std::string_view>);
    REQUIRE(name == "xin");
}

TEST_CASE("meta::Dict 支持 get_or 获取默认值", "[utility][meta][dict]")
{
    using namespace meta::literals;

    constexpr auto dict = meta::Dict{
        "name"_sym = "xin",
        "count"_sym = 7,
    };

    SECTION("当 key 存在时返回字段值")
    {
        constexpr auto name = dict.get_or<"name">(std::string_view{ "fallback" });
        constexpr auto count = dict.get_or<"count">(0);

        STATIC_REQUIRE(std::same_as<std::remove_cvref_t<decltype(name)>, std::string_view>);
        STATIC_REQUIRE(std::same_as<std::remove_cvref_t<decltype(count)>, int>);
        REQUIRE(name == "xin");
        REQUIRE(count == 7);
    }

    SECTION("当 key 不存在时返回默认值")
    {
        constexpr auto missing_text = dict.get_or<"missing_text">("fallback");
        constexpr auto missing_value = dict.get_or<"missing_value">(42);

        STATIC_REQUIRE(std::same_as<std::remove_cvref_t<decltype(missing_text)>, std::string_view>);
        STATIC_REQUIRE(std::same_as<std::remove_cvref_t<decltype(missing_value)>, int>);
        REQUIRE(missing_text == "fallback");
        REQUIRE(missing_value == 42);
    }

    SECTION("当默认值是字符串字面量时推导为 string_view")
    {
        constexpr auto missing_literal = dict.get_or<"missing_literal">("fallback");

        STATIC_REQUIRE(
            std::same_as<std::remove_cvref_t<decltype(missing_literal)>, std::string_view>);
        REQUIRE(missing_literal == "fallback");
    }

    SECTION("当默认值是可变 char 数组时推导为 string_view")
    {
        char fallback[] = "mutable";
        const auto missing_mutable = dict.get_or<"missing_mutable">(fallback);

        STATIC_REQUIRE(
            std::same_as<std::remove_cvref_t<decltype(missing_mutable)>, std::string_view>);
        REQUIRE(missing_mutable == "mutable");
    }
}

TEST_CASE("meta::Dict 支持 sym helper 构造字段", "[utility][meta][dict]")
{
    const auto dict = meta::Dict{
        meta::sym<"name">("xin"),
        meta::sym<"count">(7),
    };

    STATIC_REQUIRE(std::same_as<decltype(dict.key<"name">()), std::string_view>);
    REQUIRE(dict.key<"name">() == "xin");
    REQUIRE(dict.key<"count">() == 7);
}
