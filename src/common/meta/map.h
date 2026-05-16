#ifndef BLOG_COMMON_META_DICT_H
#define BLOG_COMMON_META_DICT_H

#include <cstddef>
#include <string_view>

#include <common/meta/string.h>

namespace meta {

/// @brief 表示 map 的单条键值项。
/// @tparam N key 字符数组大小（含结尾空字符）。
/// @tparam V value 类型。
template<std::size_t N, typename V>
struct Entry {
    /// @brief 键的非拥有字符串视图。
    std::string_view key;
    /// @brief 值对象。
    V value;

    /// @brief 由字符串字面量 key 与 value 构造 Entry。
    /// @param[in] k key 字符数组。
    /// @param[in] v value 对象。
    constexpr Entry(const char (&k)[N], V v)
      : key{ k, N - 1 }
      , value{ std::move(v) }
    {}

    /// @brief 由 string_view key 与 value 构造 Entry。
    /// @param[in] k key 视图。
    /// @param[in] v value 对象。
    constexpr Entry(std::string_view k, V v)
      : key{ k }
      , value{ std::move(v) }
    {}
};

template<std::size_t N, typename V>
Entry(const char (&)[N], V) -> Entry<N, V>;

/// @brief 基于固定 Entry 集合的轻量只读 map。
/// @tparam Value 值类型。
/// @tparam N 条目数量。
template<typename Value, std::size_t N>
class Map {
private:
    using entry_type = std::pair<std::string_view, Value>;
    std::array<entry_type, N> data_;

public:
    /// @brief 由若干 Entry 构造 map。
    /// @param[in] entries 条目序列。
    template<std::size_t... Len>
    constexpr Map(const Entry<Len, Value>&... entries)
      : data_{ entry_type{ entries.key, entries.value }... }
    {}

    /// @brief 查询指定 key 对应值。
    /// @param[in] key 目标 key。
    /// @return 命中返回 `std::optional<Value>`，否则返回空 optional。
    constexpr auto get(std::string_view key) const noexcept -> std::optional<Value>
    {
        for (const auto& [k, v] : data_)
            if (k == key)
                return v;

        return {};
    }

    /// @brief 判断是否包含指定 key。
    /// @param[in] key 目标 key。
    /// @return 包含返回 `true`，否则返回 `false`。
    constexpr auto contains(std::string_view key) const noexcept -> bool
    {
        for (const auto& [k, v] : data_)
            if (k == key)
                return true;

        return false;
    }

    /// @brief 查询 key，不存在时返回字符串默认值（`std::string_view`）。
    /// @tparam Len 默认值字符数组大小（含结尾空字符）。
    /// @param[in] key 目标 key。
    /// @param[in] default_value 默认字符串。
    /// @return 命中返回 value，缺失返回 `default_value` 的 `std::string_view`。
    template<std::size_t Len>
    constexpr auto get_or(std::string_view key, const char (&default_value)[Len]) const noexcept
        -> std::string_view
    {
        if (auto result = get(key))
            return *result;

        return std::string_view{ default_value, Len - 1 };
    }

    /// @brief 查询 key，不存在时返回给定默认值。
    /// @param[in] key 目标 key。
    /// @param[in] default_value 默认值。
    /// @return 命中返回 value，缺失返回 `default_value`。
    constexpr auto get_or(std::string_view key, Value default_value) const noexcept -> Value
    {
        if (auto result = get(key))
            return *result;

        return default_value;
    }

    /// @brief 返回条目数量。
    /// @return map 的固定大小。
    constexpr auto size() const noexcept -> std::size_t
    {
        return N;
    }

    /// @brief 判断 map 是否为空。
    /// @return 为空返回 `true`，否则返回 `false`。
    constexpr auto empty() const noexcept -> bool
    {
        return N == 0;
    }

    /// @brief 下标查询语法糖，等价于 `get(key)`。
    /// @param[in] key 目标 key。
    /// @return 查询结果 optional。
    constexpr auto operator[](std::string_view key) const noexcept -> std::optional<Value>
    {
        return get(key);
    }
};

template<std::size_t... Len, typename V>
Map(const Entry<Len, V>&...) -> Map<V, sizeof...(Len)>;

/// @brief helper：由 key 与 value 构造 Entry。
/// @tparam N key 字符数组大小（含结尾空字符）。
/// @tparam V value 类型。
/// @param[in] key key 字符数组。
/// @param[in] value value 对象。
/// @return 对应 Entry。
template<std::size_t N, typename V>
constexpr auto kv(const char (&key)[N], V&& value) -> Entry<N, std::remove_cvref_t<V>>
{
    return Entry{ key, std::forward<V>(value) };
}

/// @brief helper：由 key 与字符串字面量 value 构造 Entry。
/// @tparam N1 key 字符数组大小（含结尾空字符）。
/// @tparam N2 value 字符数组大小（含结尾空字符）。
/// @param[in] key key 字符数组。
/// @param[in] value value 字符数组。
/// @return value 推导为 `std::string_view` 的 Entry。
template<std::size_t N1, std::size_t N2>
constexpr auto kv(const char (&key)[N1], const char (&value)[N2]) -> Entry<N1, std::string_view>
{
    return Entry{ key, std::string_view{ value, N2 - 1 } };
}

/// @brief 编译期 key 对应的赋值标签。
/// @tparam Key 编译期字符串 key。
template<String Key>
struct KeyTag {
    /// @brief 将普通值绑定到 key，生成 Entry。
    /// @param[in] value 字段值。
    /// @return 构造后的 Entry。
    template<typename V>
    constexpr auto operator=(V&& value) const noexcept
    {
        return Entry<Key.capacity, std::remove_cvref_t<V>>{ Key.view(), std::forward<V>(value) };
    }

    /// @brief 将字符串字面量绑定到 key，value 推导为 `std::string_view`。
    /// @param[in] value 字符数组值。
    /// @return 构造后的 Entry。
    template<std::size_t N>
    constexpr auto operator=(const char (&value)[N]) const noexcept
    {
        return Entry<Key.capacity, std::string_view>{ Key.view(),
                                                      std::string_view{ value, N - 1 } };
    }
};

inline namespace literals {

/// @brief 生成编译期 key 对应的 `KeyTag`。
/// @tparam Key 编译期字符串 key。
/// @return 与 key 绑定的 `KeyTag`。
template<String Key>
constexpr auto operator""_key()
{
    return KeyTag<Key>{};
}

} // namespace literals

} // namespace meta

#endif // BLOG_COMMON_META_DICT_H