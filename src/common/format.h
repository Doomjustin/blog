#ifndef BLOG_COMMON_FORMAT_H
#define BLOG_COMMON_FORMAT_H

#include <format>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

#include <magic_enum/magic_enum.hpp>

template<typename T>
concept has_format_as = requires(const T& t)
{
    format_as(t);
};

/**
 * @brief Detect types that expose a `to_string()` conversion.
 */
template<typename T>
concept has_to_string = requires(const T& t)
{
    t.to_string();
};

/**
 * @brief Detect types that expose a `to_repr()` debug representation.
 */
template<typename T>
concept has_to_repr = requires(const T& t)
{
    t.to_repr();
};

/**
 * @brief Detect types streamable via `operator<<`.
 */
template<typename T>
concept has_ostream = requires (const T& t, std::ostream& os)
{
    os << t;
};

/**
 * @brief Detect class or union types (user-defined types).
 *
 * Used as a guard on `std::formatter` specializations to avoid
 * accidentally matching scalars that already have built-in formatters.
 */
template<typename T>
concept user_defined_type = std::is_class_v<std::remove_cvref_t<T>>
                         || std::is_union_v<std::remove_cvref_t<T>>;


/**
 * @brief `std::formatter` specialization for types providing `format_as()`.
 *
 * Delegates formatting to the type returned by `format_as(value)`, which
 * lets library types opt into existing formatters without writing their own.
 */
template<typename T>
    requires has_format_as<T>
struct std::formatter<T>: std::formatter<decltype(format_as(std::declval<T>()))> {
    auto format(const T& value, auto& ctx) const 
    {
        return std::formatter<decltype(format_as(value))>::format(format_as(value), ctx);
    }
};

/**
 * @brief `std::formatter` specialization for types providing `to_string()`.
 */
template <typename T>
    requires (!has_format_as<T>)
          && has_to_string<T>
struct std::formatter<T>: std::formatter<std::string> {
    auto format(const T& value, auto& ctx) const
    {
        return std::formatter<std::string>::format(value.to_string(), ctx);
    }
};

/**
 * @brief `std::formatter` specialization for types providing `to_repr()`.
 */
template<typename T>
    requires (!has_format_as<T>) 
          && (!has_to_string<T>) 
          && has_to_repr<T>
struct std::formatter<T>: std::formatter<std::string> {
    auto format(const T& value, auto& ctx) const
    {
        return std::formatter<std::string>::format(value.to_repr(), ctx);
    }
};

/**
 * @brief `std::formatter` specialization for enum types via `magic_enum`.
 *
 * Formats enum values as their string name rather than their underlying
 * integer, improving log readability without manual switch tables.
 */
template<typename T>
    requires (!has_format_as<T>) 
          && (!has_to_string<T>) 
          && (!has_to_repr<T>)
          && std::is_enum_v<T>
struct std::formatter<T>: std::formatter<std::string_view> {
    auto format(const T& value, auto& ctx) const
    {
        return std::formatter<std::string_view>::format(magic_enum::enum_name(value), ctx);
    }
};

/**
 * @brief `std::formatter` specialization for ostream-compatible user types.
 *
 * Fallback for user-defined types that have `operator<<` but none of the
 * preferred hook points (`format_as`, `to_string`, `to_repr`).
 */
template<typename T>
    requires (!has_format_as<T>) 
          && (!has_to_string<T>) 
          && (!has_to_repr<T>) 
          && user_defined_type<T>
          && has_ostream<T>
struct std::formatter<T>: std::formatter<std::string> {
    auto format(const T& value, auto& ctx) const
    {        
        std::ostringstream os;
        os << value;
        return std::formatter<std::string>::format(os.str(), ctx);
    }
};

#endif // BLOG_COMMON_FORMAT_H