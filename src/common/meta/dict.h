#ifndef BLOG_COMMON_META_DICT_H
#define BLOG_COMMON_META_DICT_H

#include <cstddef>
#include <string_view>

#include <common/meta/string.h>

namespace meta {

/// @brief 表示一个编译期 key 与运行期 value 的字段。
/// @tparam Key 编译期字符串 key。
/// @tparam Value 字段值类型。
template<String Key, typename Value>
struct Field {
    /// @brief 字段的编译期 key。
    static constexpr auto key = Key;
    /// @brief 字段值。
    Value value;

    /// @brief 默认构造字段。
    constexpr Field() = default;

    /// @brief 从输入值构造字段。
    /// @param[in] input 用于初始化字段值的对象。
    template<typename V>
        requires std::constructible_from<Value, V&&>
    constexpr explicit Field(V&& input)
      : value{ std::forward<V>(input) }
    {}
};

/// @brief 编译期 key 驱动的轻量字典。
/// @tparam Fields 由 Field 组成的字段集合。
template<typename... Fields>
class Dict : Fields... {
private:
    template<String Key, typename V>
    static constexpr auto get_impl(const Field<Key, V>& field) -> decltype(auto)
    {
        return field.value;
    }

    template<String Key, typename V>
    static constexpr auto has_impl(const Field<Key, V>&) -> std::true_type;

    static constexpr auto has_impl(...) -> std::false_type;

public:
    /// @brief 从字段集合构造 Dict。
    /// @param[in] fields 要存入字典的字段对象。
    constexpr Dict(Fields... fields)
      : Fields{ std::move(fields) }...
    {}

    /// @brief 按 key 获取字段值。
    /// @tparam Key 目标字段的编译期 key。
    /// @return 对应字段值（按值返回）。
    template<String Key>
    constexpr auto key() const
    {
        return get_impl<Key>(*this);
    }

    /// @brief 检查字典是否包含给定 key。
    /// @tparam Key 待检查的编译期 key。
    /// @return 若存在返回 `true`，否则返回 `false`。
    template<String Key>
    constexpr auto contains() const noexcept
    {
        return requires(const Dict& dict) { get_impl<Key>(dict); };
    }

    /// @brief 获取 key 对应值，缺失时返回调用方给定默认值。
    /// @tparam Key 目标字段的编译期 key。
    /// @tparam DefaultType 默认值类型。
    /// @param[in] default_value 当 key 缺失时返回的默认值。
    /// @return 若 key 存在返回字段值，否则返回 `default_value`。
    template<String Key, typename DefaultType>
    constexpr auto get_or(DefaultType&& default_value) const
    {
        if constexpr (requires(const Dict& dict) { get_impl<Key>(dict); })
            return key<Key>();
        else
            return std::forward<DefaultType>(default_value);
    }

    /// @brief 当默认值为 `const char[N]` 时获取值或返回 `std::string_view` 默认值。
    /// @tparam Key 目标字段的编译期 key。
    /// @tparam N 字符数组大小（含结尾空字符）。
    /// @param[in] default_value 缺失时返回的字符串默认值。
    /// @return 若 key 存在返回字段值，否则返回指向默认值的 `std::string_view`。
    template<String Key, std::size_t N>
    constexpr auto get_or(const char (&default_value)[N]) const noexcept
    {
        if constexpr (requires(const Dict& dict) { get_impl<Key>(dict); })
            return key<Key>();
        else
            return std::string_view{ default_value, N - 1 };
    }

    /// @brief 当默认值为 `char[N]` 时获取值或返回 `std::string_view` 默认值。
    /// @tparam Key 目标字段的编译期 key。
    /// @tparam N 字符数组大小（含结尾空字符）。
    /// @param[in] default_value 缺失时返回的字符串默认值。
    /// @return 若 key 存在返回字段值，否则返回指向默认值的 `std::string_view`。
    template<String Key, std::size_t N>
    constexpr auto get_or(char (&default_value)[N]) const noexcept
    {
        if constexpr (requires(const Dict& dict) { get_impl<Key>(dict); })
            return key<Key>();
        else
            return std::string_view{ default_value, N - 1 };
    }

    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof...(Fields);
    }
};

template<typename... Fields>
Dict(Fields...) -> Dict<Fields...>;

/// @brief 为编译期 key 提供赋值语法糖，生成 Field。
/// @tparam Key 编译期字符串 key。
template<String Key>
struct SymbolTag {
    /// @brief 将 `const char[N]` 赋给 key，值类型推导为 `std::string_view`。
    /// @param[in] value 字符数组值。
    /// @return 构造后的 Field。
    template<std::size_t N>
    constexpr auto operator=(const char (&value)[N]) const noexcept
    {
        return Field<Key, std::string_view>{ std::string_view{ value, N - 1 } };
    }

    /// @brief 将 `char[N]` 赋给 key，值类型推导为 `std::string_view`。
    /// @param[in] value 字符数组值。
    /// @return 构造后的 Field。
    template<std::size_t N>
    constexpr auto operator=(char (&value)[N]) const noexcept
    {
        return Field<Key, std::string_view>{ std::string_view{ value, N - 1 } };
    }

    /// @brief 将非字符 C 数组赋给 key，值类型推导为 `std::array`。
    /// @param[in] value C 数组值。
    /// @return 构造后的 Field。
    template<typename Element, std::size_t N>
        requires(!std::same_as<std::remove_cv_t<Element>, char>)
    constexpr auto operator=(const Element (&value)[N]) const
    {
        return Field<Key, std::array<std::remove_cv_t<Element>, N>>{ std::to_array(value) };
    }

    /// @brief 将普通值赋给 key，值类型按去 cvref 后推导。
    /// @param[in] value 要写入字段的值。
    /// @return 构造后的 Field。
    template<typename V>
        requires(!std::is_array_v<std::remove_reference_t<V>>)
    constexpr auto operator=(V&& value) const noexcept
    {
        return Field<Key, std::remove_cvref_t<V>>{ std::forward<V>(value) };
    }
};

/// @brief 使用编译期 key 直接构造字段，便于函数式写法。
/// @tparam Key 编译期字符串 key。
/// @tparam V 字段值类型。
/// @param[in] value 字段值。
/// @return 与 key 绑定的 Field。
template<String Key, typename V>
constexpr auto sym(V&& value) noexcept(noexcept(SymbolTag<Key>{} = std::forward<V>(value)))
{
    return SymbolTag<Key>{} = std::forward<V>(value);
}

inline namespace literals {

/// @brief 生成编译期 key 对应的 SymbolTag。
/// @tparam Key 编译期字符串 key。
/// @return 与 key 绑定的 SymbolTag。
template<String Key>
constexpr auto operator""_sym()
{
    return SymbolTag<Key>{};
}

} // namespace literals

} // namespace meta

#endif // BLOG_COMMON_META_DICT_H