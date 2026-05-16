#include "map.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("meta::Map 支持基本查询接口", "[utility][meta][map]")
{
    constexpr meta::Map<int, 3> map{
        meta::Entry{ "one", 1 },
        meta::Entry{ "two", 2 },
        meta::Entry{ "three", 3 },
    };

    STATIC_REQUIRE(map.size() == 3U);
    STATIC_REQUIRE(map.contains("one"));
    STATIC_REQUIRE(map.contains("three"));
    STATIC_REQUIRE_FALSE(map.contains("missing"));

    REQUIRE(map.size() == 3U);
    REQUIRE(map.contains("one"));
    REQUIRE(map.contains("three"));
    REQUIRE_FALSE(map.contains("missing"));
}

TEST_CASE("meta::Map 的 get 在存在和缺失 key 时返回预期 optional", "[utility][meta][map]")
{
    constexpr meta::Map<int, 2> map{
        meta::Entry{ "alpha", 11 },
        meta::Entry{ "beta", 22 },
    };

    SECTION("命中 key 返回值")
    {
        constexpr auto found = map.get("alpha");
        STATIC_REQUIRE(found.has_value());
        STATIC_REQUIRE(*found == 11);

        REQUIRE(found.has_value());
        REQUIRE(*found == 11);
    }

    SECTION("缺失 key 返回空 optional")
    {
        constexpr auto missing = map.get("gamma");
        STATIC_REQUIRE_FALSE(missing.has_value());

        REQUIRE_FALSE(missing.has_value());
    }
}

TEST_CASE("meta::Map 的 get_or 在 key 缺失时返回默认值", "[utility][meta][map]")
{
    constexpr meta::Map<std::string_view, 2> map{
        meta::Entry{ "name", std::string_view{ "xin" } },
        meta::Entry{ "lang", std::string_view{ "cpp" } },
    };

    SECTION("命中 key 返回映射值")
    {
        constexpr auto value = map.get_or("name", std::string_view{ "fallback" });
        STATIC_REQUIRE(value == std::string_view{ "xin" });
        REQUIRE(value == "xin");
    }

    SECTION("缺失 key 返回默认值")
    {
        constexpr auto value = map.get_or("missing", std::string_view{ "fallback" });
        STATIC_REQUIRE(value == std::string_view{ "fallback" });
        REQUIRE(value == "fallback");
    }
}

TEST_CASE("meta::Map 支持 _key literals 构造 Entry", "[utility][meta][map]")
{
    using namespace meta::literals;

    const auto map = meta::Map{
        "name"_key = "xin",
        "lang"_key = "cpp",
    };

    REQUIRE(map.contains("name"));
    REQUIRE(map.get_or("name", "fallback") == "xin");
    REQUIRE(map.get_or("missing", "fallback") == "fallback");
}

TEST_CASE("meta::Map 支持 kv helper 构造 Entry", "[utility][meta][map]")
{
    const auto map = meta::Map{
        meta::kv("name", "xin"),
        meta::kv("lang", "cpp"),
    };

    REQUIRE(map.contains("name"));
    REQUIRE(map.get_or("name", "fallback") == "xin");
    REQUIRE(map.get_or("missing", "fallback") == "fallback");
}
