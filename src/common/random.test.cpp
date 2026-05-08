#include <common/random.h>

#include <algorithm>
#include <array>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("random: seed makes integral uniform reproducible", "[random]")
{
    random::seed(12345U);
    const auto a1 = random::uniform(0, 100);
    const auto a2 = random::uniform(0, 100);
    const auto a3 = random::uniform(0, 100);

    random::seed(12345U);
    const auto b1 = random::uniform(0, 100);
    const auto b2 = random::uniform(0, 100);
    const auto b3 = random::uniform(0, 100);

    REQUIRE(a1 == b1);
    REQUIRE(a2 == b2);
    REQUIRE(a3 == b3);
}

TEST_CASE("random: bernoulli is reproducible with seed", "[random]")
{
    random::seed(77U);
    const auto first = random::bernoulli(0.25);

    random::seed(77U);
    const auto second = random::bernoulli(0.25);

    REQUIRE(first == second);
}

TEST_CASE("random: integral uniform stays in [low, high)", "[random]")
{
    random::seed(1U);

    constexpr int low = -3;
    constexpr int high = 7;
    for (int i = 0; i < 256; ++i) {
        const auto value = random::uniform(low, high);
        REQUIRE(value >= low);
        REQUIRE(value < high);
    }
}

TEST_CASE("random: integral uniform(high) stays in [0, high)", "[random]")
{
    random::seed(2U);

    constexpr int high = 9;
    for (int i = 0; i < 256; ++i) {
        const auto value = random::uniform(high);
        REQUIRE(value >= 0);
        REQUIRE(value < high);
    }
}

TEST_CASE("random: floating uniform stays in [low, high)", "[random]")
{
    random::seed(3U);

    constexpr double low = -1.5;
    constexpr double high = 2.5;
    for (int i = 0; i < 256; ++i) {
        const auto value = random::uniform(low, high);
        REQUIRE(value >= low);
        REQUIRE(value < high);
    }
}

TEST_CASE("random: geometric_failure and binomial stay in valid ranges", "[random]")
{
    random::seed(4U);

    for (int i = 0; i < 128; ++i) {
        const auto geo = random::geometric_failure<int>(0.4);
        REQUIRE(geo >= 0);

        const auto bino = random::binomial<int>(10, 0.3);
        REQUIRE(bino >= 0);
        REQUIRE(bino <= 10);
    }
}

TEST_CASE("random: exponential is non-negative", "[random]")
{
    random::seed(5U);

    for (int i = 0; i < 128; ++i) {
        const auto value = random::exponential<double>(2.0);
        REQUIRE(value >= 0.0);
    }
}

TEST_CASE("random: shuffle preserves multiset of elements", "[random]")
{
    std::vector<int> values{ 1, 2, 2, 3, 5, 8 };
    const auto original = values;

    random::seed(6U);
    random::shuffle(values);

    auto sorted_original = original;
    auto sorted_shuffled = values;
    std::sort(sorted_original.begin(), sorted_original.end());
    std::sort(sorted_shuffled.begin(), sorted_shuffled.end());

    REQUIRE(sorted_shuffled == sorted_original);
}

TEST_CASE("random: choice returns an element from input range", "[random]")
{
    const std::array<int, 4> values{ 10, 20, 30, 40 };

    random::seed(7U);
    const int chosen = random::choice(values);

    REQUIRE(std::ranges::find(values, chosen) != values.end());
}

TEST_CASE("random: sample returns requested count and subset of source", "[random]")
{
    std::vector<int> values{ 1, 2, 3, 4, 5, 6 };

    random::seed(8U);
    const auto sampled = random::sample(values, static_cast<std::vector<int>::size_type>(3));

    REQUIRE(sampled.size() == 3);

    std::multiset<int> source(values.begin(), values.end());
    for (const int value : sampled)
        REQUIRE(source.contains(value));
}
