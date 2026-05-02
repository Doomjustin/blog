#ifndef BLOG_COMMON_CODING_H
#define BLOG_COMMON_CODING_H

#include <concepts>
#include <cstdint>
#include <iterator>

namespace detail {

static constexpr int shift_bits = 7;
static constexpr auto mask = 0b01111111;
static constexpr auto continuation = 0b10000000;

constexpr auto is_continuation(const std::uint8_t byte) noexcept -> bool
{
    return byte & continuation;
}

} // namespace detail

/**
 * @brief Variable-length integer codec using LEB128 encoding.
 *
 * Small values occupy fewer bytes: values 0–127 fit in 1 byte, 128–16383
 * in 2 bytes, and so on. This makes it efficient for protocol buffers or
 * any stream where small integers dominate.
 *
 * The struct is non-constructible; all methods are static.
 *
 * Example:
 * @code
 * std::vector<std::byte> buf;
 * varint::encode(std::back_inserter(buf), 300U);
 * // buf == { 0xAC, 0x02 }  (LEB128 for 300)
 *
 * auto [val, _] = varint::decode<unsigned>(buf.begin());
 * // val == 300
 * @endcode
 */
struct varint {
    varint() = delete;

    /**
     * @brief Returns the number of *additional* bytes beyond the first needed
     *        to encode `value` in LEB128.
     *
     * Equivalently, `length(v) + 1` is the total encoded byte count.
     *
     * @param value  The unsigned integer to measure.
     * @return 0 if `value < 128`, otherwise the number of continuation bytes.
     */
    template<std::unsigned_integral T>
    static auto length(T value) noexcept -> std::size_t
    {
        std::size_t result = 0;
        while (value >= 128) {
            value >>= 7;
            ++result;
        }

        return result;
    }

    /**
     * @brief Encodes `value` in LEB128 into a `std::byte` output iterator.
     *
     * @param iter   Output iterator accepting `std::byte`.
     * @param value  Unsigned integer to encode.
     * @return Iterator pointing one past the last written byte.
     */
    template<typename Iterator, std::unsigned_integral T>
        requires std::output_iterator<Iterator, std::byte>
    static auto encode(Iterator iter, T value) noexcept -> Iterator
    {
        while (value > detail::mask) {
            // 写入一个字节，最高位为1表示后面还有字节
            auto byte = static_cast<std::byte>((value & detail::mask) | detail::continuation);
            *iter++ = byte;
            value >>= detail::shift_bits;
        }

        *iter++ = static_cast<std::byte>(value); // 最后一个字节，最高位为0
        return iter;
    }

    /**
     * @brief Encodes `value` in LEB128 into a `char` output iterator.
     *
     * Identical to the `std::byte` overload but for char-based streams.
     *
     * @param iter   Output iterator accepting `char`.
     * @param value  Unsigned integer to encode.
     * @return Iterator pointing one past the last written byte.
     */
    template<typename Iterator, std::unsigned_integral T>
        requires std::output_iterator<Iterator, char>
    static auto encode(Iterator iter, T value) noexcept -> Iterator
    {
        while (value > detail::mask) {
            auto byte = static_cast<char>((value & detail::mask) | detail::continuation);
            *iter++ = byte;
            value >>= detail::shift_bits;
        }

        *iter++ = static_cast<char>(value); // 最后一个字节，最高位为0
        return iter;
    }

    /**
     * @brief Decodes a LEB128-encoded value from a `std::byte` input iterator.
     *
     * Reads bytes until it encounters one without the continuation bit set.
     *
     * @tparam T     Target unsigned integer type to decode into.
     * @param iter   Input iterator over `std::byte`.
     * @return `{decoded_value, iterator_past_last_read_byte}`.
     *
     * @pre The iterator range contains a complete, valid LEB128 sequence.
     */
    template<std::unsigned_integral T, std::input_iterator Iterator>
        requires std::convertible_to<typename std::iterator_traits<Iterator>::value_type, std::byte>
    static auto decode(Iterator iter) noexcept -> std::pair<T, Iterator>
    {
        T value = 0;
        int shift = 0;
        while (true) {
            auto byte_value = std::to_integer<T>(*iter++);
            value |= (byte_value & detail::mask) << shift;

            if (!detail::is_continuation(byte_value))
                break;

            shift += detail::shift_bits;
        }

        return { value, iter };
    }

    /**
     * @brief Decodes a LEB128-encoded value from a `char` input iterator.
     *
     * Identical to the `std::byte` overload but for char-based streams.
     *
     * @tparam T     Target unsigned integer type to decode into.
     * @param iter   Input iterator over `char` or `const char*`.
     * @return `{decoded_value, iterator_past_last_read_byte}`.
     *
     * @pre The iterator range contains a complete, valid LEB128 sequence.
     */
    template<std::unsigned_integral T, std::input_iterator Iterator>
        requires std::convertible_to<typename std::iterator_traits<Iterator>::value_type, char> 
              || std::is_same_v<Iterator, const char*>
    static auto decode(Iterator iter) noexcept -> std::pair<T, Iterator>
    {
        T value = 0;
        int shift = 0;
        while (true) {
            const auto byte_value = static_cast<std::uint8_t>(*iter++);
            value |= (byte_value & detail::mask) << shift;

            if (!detail::is_continuation(byte_value))
                break;

            shift += detail::shift_bits;
        }

        return { value, iter };
    }
};


/**
 * @brief Fixed-width integer codec using little-endian byte order.
 *
 * Encodes a `sizeof(T)`-byte value in little-endian order regardless of
 * host endianness. This guarantees portable wire representations when
 * reading/writing binary protocols.
 *
 * The struct is non-constructible; all methods are static.
 *
 * Example:
 * @code
 * std::vector<std::byte> buf;
 * fixed::encode(std::back_inserter(buf), static_cast<uint16_t>(0x0102));
 * // buf == { 0x02, 0x01 }  (little-endian)
 *
 * auto [val, _] = fixed::decode<uint16_t>(buf.begin());
 * // val == 0x0102
 * @endcode
 */
struct fixed {
    fixed() = delete;

    /**
     * @brief Encodes `value` in little-endian order into a `std::byte` output iterator.
     *
     * Writes exactly `sizeof(T)` bytes.
     *
     * @param iter   Output iterator accepting `std::byte`.
     * @param value  Unsigned integer to encode.
     * @return Iterator pointing one past the last written byte.
     */
    template<std::unsigned_integral T, std::output_iterator<std::byte> Iterator>
    static auto encode(Iterator iter, T value) noexcept -> Iterator
    {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            *iter++ = static_cast<std::byte>(value & 0xFF);
            value >>= 8;
        }

        return iter;
    }

    /**
     * @brief Encodes `value` in little-endian order into a `char` output iterator.
     *
     * Identical to the `std::byte` overload but for char-based streams.
     * Writes exactly `sizeof(T)` bytes.
     *
     * @param iter   Output iterator accepting `char`.
     * @param value  Unsigned integer to encode.
     * @return Iterator pointing one past the last written byte.
     */
    template<typename Iterator, std::unsigned_integral T>
        requires std::output_iterator<Iterator, char>
    static auto encode(Iterator iter, T value) noexcept -> Iterator
    {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            *iter++ = static_cast<char>(value & 0xFF);

            if constexpr (sizeof(T) > 1)
                value >>= 8;
        }

        return iter;
    }

    /**
     * @brief Decodes a little-endian fixed-width integer from a `std::byte` iterator.
     *
     * Reads exactly `sizeof(T)` bytes.
     *
     * @tparam T     Target unsigned integer type.
     * @param iter   Input iterator over `std::byte`.
     * @return `{decoded_value, iterator_past_last_read_byte}`.
     */
    template<std::unsigned_integral T, std::input_iterator Iterator>
        requires std::convertible_to<typename std::iterator_traits<Iterator>::value_type, std::byte>
    static auto decode(Iterator iter) noexcept -> std::pair<T, Iterator>
    {
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= (static_cast<T>(std::to_integer<std::uint8_t>(*iter++)) << (8 * i));

        return { value, iter };
    }

    /**
     * @brief Decodes a little-endian fixed-width integer from a `char` iterator.
     *
     * Identical to the `std::byte` overload but for char-based streams.
     * Reads exactly `sizeof(T)` bytes.
     *
     * @tparam T     Target unsigned integer type.
     * @param iter   Input iterator over `char` or `const char*`.
     * @return `{decoded_value, iterator_past_last_read_byte}`.
     */
    template<std::unsigned_integral T, std::input_iterator Iterator>
        requires std::convertible_to<typename std::iterator_traits<Iterator>::value_type, char> 
              || std::is_same_v<Iterator, const char*>
    static auto decode(Iterator iter) noexcept -> std::pair<T, Iterator>
    {
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= (static_cast<T>(static_cast<std::uint8_t>(*iter++)) << (8 * i));

        return { value, iter };
    }
};


/**
 * @brief Tag type carrying an N-byte payload to be packed into a wider integer.
 *
 * Constructed via `pack_bytes()`; consumed by `pack()` and `unpack()`.
 *
 * @tparam N  Number of bytes to represent (1–sizeof(T)-1).
 * @tparam T  Unsigned integer type of the payload.
 */
template<std::uint8_t N, std::unsigned_integral T>
struct PackBytesT {
    T value;
};

/**
 * @brief Creates a `PackBytesT` tag for use with `pack()`.
 *
 * @tparam N      Number of low-order bytes of `value` to pack.
 * @param value   Source value; only the low N bytes are used.
 */
template<std::uint8_t N, std::unsigned_integral T>
constexpr auto pack_bytes(T value) noexcept -> PackBytesT<N, T>
{
    return PackBytesT<N, T>{ value };
}

/**
 * @brief Shifts `dest` left by N bytes and inserts the low N bytes of `src`.
 *
 * Intended for building a packed integer field by field:
 * @code
 * uint32_t packed = 0;
 * packed = pack(packed, pack_bytes<1>(0xAAU));  // packed == 0x000000AA
 * packed = pack(packed, pack_bytes<1>(0xBBU));  // packed == 0x0000AABB
 * @endcode
 *
 * @tparam N  Number of bytes to shift and insert (must satisfy `0 < N < sizeof(T)`).
 * @param dest  Accumulator value to shift.
 * @param src   Tagged source value produced by `pack_bytes<N>()`.
 * @return New packed value with the low N bytes of `src` inserted.
 */
template<std::unsigned_integral T, std::unsigned_integral U, std::uint8_t N>
    requires(N > 0 && N < sizeof(T) && N <= sizeof(U))
constexpr auto pack(T dest, PackBytesT<N, U> src) noexcept -> T
{
    constexpr auto shift = 8 * N;
    constexpr auto mask = (T{ 1 } << shift) - 1;
    return (dest << shift) | (static_cast<T>(src.value) & mask);
}

/**
 * @brief Extracts the low N bytes from `packed_value` and returns the remainder.
 *
 * The inverse of `pack()`. Peel fields off in the reverse order they were packed:
 * @code
 * auto [b, rest] = unpack<uint8_t, 1>(packed);  // extracts low byte
 * @endcode
 *
 * @tparam U  Unsigned type to extract into.
 * @tparam N  Number of bytes to extract from the low end.
 * @param packed_value  The packed integer to extract from.
 * @return `{extracted_value, remaining_shifted_value}`.
 */
template<std::unsigned_integral U, std::uint8_t N, std::unsigned_integral T>
    requires(N > 0 && N < sizeof(T) && N <= sizeof(U))
constexpr auto unpack(const T& packed_value) noexcept -> std::pair<U, T>
{
    constexpr auto shift = 8 * N;
    constexpr auto mask = (T{ 1 } << shift) - 1;
    const auto extracted = static_cast<U>(packed_value & mask);
    const auto remaining = packed_value >> shift;
    return { extracted, remaining };
}

#endif // BLOG_COMMON_CODING_H