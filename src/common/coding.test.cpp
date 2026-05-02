#include "coding.h"

#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// ------- varint::length -----------------------------------------------------

TEST_CASE("varint::length: single-byte values (0–127)", "[varint]")
{
    REQUIRE(varint::length(0U) == 0);
    REQUIRE(varint::length(127U) == 0);
}

TEST_CASE("varint::length: two-byte boundary (128)", "[varint]")
{
    REQUIRE(varint::length(128U) == 1);
}

TEST_CASE("varint::length: three-byte boundary (16384)", "[varint]")
{
    REQUIRE(varint::length(16384U) == 2);
}

// ------- varint encode / decode (std::byte) ---------------------------------

TEST_CASE("varint: encode/decode round-trip for single-byte value", "[varint]")
{
    std::array<std::byte, 8> buf{};
    auto* end = varint::encode(buf.begin(), 42U);
    REQUIRE(end == buf.begin() + 1);

    auto [value, _] = varint::decode<unsigned>(buf.begin());
    REQUIRE(value == 42U);
}

TEST_CASE("varint: encode/decode round-trip for two-byte value (300)", "[varint]")
{
    std::array<std::byte, 8> buf{};
    auto* end = varint::encode(buf.begin(), 300U);
    REQUIRE(end == buf.begin() + 2);

    auto [value, _] = varint::decode<unsigned>(buf.begin());
    REQUIRE(value == 300U);
}

TEST_CASE("varint: encode/decode round-trip for zero", "[varint]")
{
    std::array<std::byte, 8> buf{};
    varint::encode(buf.begin(), 0U);
    auto [value, _] = varint::decode<unsigned>(buf.begin());
    REQUIRE(value == 0U);
}

TEST_CASE("varint: encode/decode round-trip for uint64 large value", "[varint]")
{
    std::array<std::byte, 16> buf{};
    constexpr std::uint64_t original = 0xDEADBEEF'12345678ULL;
    varint::encode(buf.begin(), original);
    auto [value, _] = varint::decode<std::uint64_t>(buf.begin());
    REQUIRE(value == original);
}

// ------- varint encode / decode (char) --------------------------------------

TEST_CASE("varint: encode/decode round-trip via char iterator", "[varint]")
{
    std::vector<char> buf(8, '\0');
    auto end = varint::encode(buf.begin(), 300U);
    REQUIRE(end == buf.begin() + 2);

    auto [value, _] = varint::decode<unsigned>(buf.begin());
    REQUIRE(value == 300U);
}

// ------- fixed encode / decode (std::byte) ----------------------------------

TEST_CASE("fixed: encode/decode uint8 round-trip", "[fixed]")
{
    std::array<std::byte, 1> buf{};
    fixed::encode(buf.begin(), std::uint8_t{ 0xAB });
    auto [value, _] = fixed::decode<std::uint8_t>(buf.begin());
    REQUIRE(value == 0xABU);
}

TEST_CASE("fixed: encode/decode uint16 little-endian", "[fixed]")
{
    std::array<std::byte, 2> buf{};
    fixed::encode(buf.begin(), std::uint16_t{ 0x1234 });
    REQUIRE(buf[0] == std::byte{ 0x34 }); // low byte first
    REQUIRE(buf[1] == std::byte{ 0x12 });

    auto [value, _] = fixed::decode<std::uint16_t>(buf.begin());
    REQUIRE(value == 0x1234U);
}

TEST_CASE("fixed: encode/decode uint32 round-trip", "[fixed]")
{
    std::array<std::byte, 4> buf{};
    fixed::encode(buf.begin(), std::uint32_t{ 0xDEADBEEF });
    auto [value, _] = fixed::decode<std::uint32_t>(buf.begin());
    REQUIRE(value == 0xDEADBEEFU);
}

// ------- pack / unpack ------------------------------------------------------

TEST_CASE("pack/unpack: 1-byte payload into uint32", "[pack]")
{
    constexpr auto packed = pack(std::uint32_t{ 0 }, pack_bytes<1>(std::uint8_t{ 0xAB }));
    auto [extracted, remaining] = unpack<std::uint8_t, 1>(packed);
    REQUIRE(extracted == 0xABU);
    REQUIRE(remaining == 0U);
}

TEST_CASE("pack/unpack: two successive packs", "[pack]")
{
    // Pack 0xCD (1 byte) then 0xAB (1 byte) into a uint32
    auto p1 = pack(std::uint32_t{ 0 }, pack_bytes<1>(std::uint8_t{ 0xCD }));
    auto p2 = pack(p1, pack_bytes<1>(std::uint8_t{ 0xAB }));

    auto [first, after_first] = unpack<std::uint8_t, 1>(p2);
    auto [second, _] = unpack<std::uint8_t, 1>(after_first);

    REQUIRE(first == 0xABU);
    REQUIRE(second == 0xCDU);
}
