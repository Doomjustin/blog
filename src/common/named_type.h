#ifndef BLOG_COMMON_NAMED_TYPE_H
#define BLOG_COMMON_NAMED_TYPE_H

#include <cmath>
#include <concepts>
#include <cstddef>

#include <fixed_string.h>

/**
 * @brief Strong typedef wrapper that prevents implicit unit confusion.
 *
 * Wraps an arithmetic value `T` under a compile-time `Name` tag so that
 * types with identical underlying representations cannot be silently mixed.
 * Capabilities are added à la carte via the `Skills` pack:
 * `Arithmetic`, `Comparable`, `Bitwise`, `Hashable`, `Printable`.
 *
 * @tparam T     Underlying arithmetic type (int, double, float, …).
 * @tparam Name  Compile-time string tag that makes each instantiation unique.
 * @tparam Skills  Zero or more CRTP skill templates to mix in.
 *
 * Example:
 * @code
 * using Meter   = NamedType<double, "Meter",   Arithmetic, Comparable>;
 * using Kilogram = NamedType<double, "Kilogram", Arithmetic, Comparable>;
 *
 * Meter   dist{ 5.0 };
 * Kilogram mass{ 3.0 };
 * // dist + mass;  // compile error: different types
 * @endcode
 */
template <typename T, FixedString Name, template <typename> class... Skills>
requires std::is_arithmetic_v<T>
class NamedType: public Skills<NamedType<T, Name, Skills...>>... {
public:
    using value_type = T;

    explicit constexpr NamedType(const T& v) noexcept
      : value_{v}
    {}

    explicit constexpr NamedType(T&& v) noexcept
      : value_{std::move(v)}
    {}

    [[nodiscard]]
    constexpr auto get() noexcept -> T& { return value_; }

    [[nodiscard]]
    constexpr auto get() const noexcept -> const T& { return value_; }

    [[nodiscard]]
    auto operator*() noexcept -> T& { return value_; }

    [[nodiscard]]
    auto operator*() const noexcept -> const T& { return value_; }

private:
    T value_;
};

/** @brief Skill: adds `/` and `/=` operators between two values of the same NamedType. */
template<typename Derived>
struct Dividable {
    constexpr auto operator/=(const Derived& other) noexcept -> Derived&
    {
        static_cast<Derived*>(this)->get() /= other.get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator/(Derived lhs, const Derived& rhs) noexcept -> Derived
    {
        lhs /= rhs;
        return lhs;
    }
};

/** @brief Skill: adds prefix and postfix `--` operators. */
template<typename Derived>
struct Decrementable {
    constexpr auto operator--() noexcept -> Derived&
    {
        --static_cast<Derived*>(this)->get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    constexpr auto operator--(int) noexcept -> Derived
    {
        Derived temp = *static_cast<Derived*>(this);
        --static_cast<Derived*>(this)->get();
        return temp;
    }
};

/** @brief Skill: adds prefix and postfix `++` operators. */
template<typename Derived>
struct Incrementable {
    constexpr auto operator++() noexcept -> Derived&
    {
        ++static_cast<Derived*>(this)->get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    constexpr auto operator++(int) noexcept -> Derived
    {
        Derived temp = *static_cast<Derived*>(this);
        ++static_cast<Derived*>(this)->get();
        return temp;
    }
};

/** @brief Skill: adds `+` and `+=` operators between two values of the same NamedType. */
template<typename Derived>
struct Addable {
    constexpr auto operator+=(const Derived& other) noexcept -> Derived&
    {
        static_cast<Derived*>(this)->get() += other.get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator+(Derived lhs, const Derived& rhs) noexcept -> Derived
    {
        lhs += rhs;
        return lhs;
    }
};

/** @brief Skill: adds `-` and `-=` operators between two values of the same NamedType. */
template<typename Derived>
struct Subtractable {
    constexpr auto operator-=(const Derived& other) noexcept -> Derived&
    {
        static_cast<Derived*>(this)->get() -= other.get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator-(Derived lhs, const Derived& rhs) noexcept -> Derived
    {
        lhs -= rhs;
        return lhs;
    }
};

/** @brief Skill: adds `*` and `*=` operators between two values of the same NamedType. */
template<typename Derived>
struct Multipliable {
    constexpr auto operator*=(const Derived& other) noexcept -> Derived&
    {
        static_cast<Derived*>(this)->get() *= other.get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator*(Derived lhs, const Derived& rhs) noexcept -> Derived
    {
        lhs *= rhs;
        return lhs;
    }
};

/**
 * @brief Skill: adds `%` and `%=` operators.
 *
 * For integral types uses the built-in `%`; for floating-point types
 * delegates to `std::fmod`.
 */
template<typename Derived>
struct RemainderAssignable {
    constexpr auto operator%=(const Derived& other) noexcept -> Derived&
        requires std::integral<typename Derived::value_type>
    {
        static_cast<Derived*>(this)->get() %= other.get();
        return *static_cast<Derived*>(this);
    }

    constexpr auto operator%=(const Derived& other) noexcept -> Derived&
        requires std::floating_point<typename Derived::value_type>
    {
        static_cast<Derived*>(this)->get() = std::fmod(static_cast<Derived*>(this)->get(), other.get());
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator%(Derived lhs, const Derived& rhs) noexcept -> Derived
        requires std::integral<typename Derived::value_type> ||
                 std::floating_point<typename Derived::value_type>
    {
        lhs %= rhs;
        return lhs;
    }
};

/**
 * @brief Skill bundle: combines all arithmetic skills.
 *
 * Equivalent to inheriting from `Decrementable`, `Incrementable`,
 * `Addable`, `Subtractable`, `Multipliable`, `Dividable`, and
 * `RemainderAssignable` individually.
 */
template<typename Derived>
struct Arithmetic : Decrementable<Derived>,
                    Incrementable<Derived>,
                    Addable<Derived>,
                    Subtractable<Derived>,
                    Multipliable<Derived>,
                    Dividable<Derived>,
                    RemainderAssignable<Derived>
{};

/** @brief Skill: adds `&` and `&=` operators (integral types only). */
template<typename Derived>
struct BitwiseAndAssignable {
    constexpr auto operator&=(const Derived& other) noexcept -> Derived&
        requires std::integral<typename Derived::value_type>
    {
        static_cast<Derived*>(this)->get() &= other.get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator&(Derived lhs, const Derived& rhs) noexcept -> Derived
        requires std::integral<typename Derived::value_type>
    {
        lhs &= rhs;
        return lhs;
    }
};

/** @brief Skill: adds `|` and `|=` operators (integral types only). */
template<typename Derived>
struct BitwiseOrAssignable {
    constexpr auto operator|=(const Derived& other) noexcept -> Derived&
        requires std::integral<typename Derived::value_type>
    {
        static_cast<Derived*>(this)->get() |= other.get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator|(Derived lhs, const Derived& rhs) noexcept -> Derived
        requires std::integral<typename Derived::value_type>
    {
        lhs |= rhs;
        return lhs;
    }
};

/** @brief Skill: adds `^` and `^=` operators (integral types only). */
template<typename Derived>
struct BitwiseXorAssignable {
    constexpr auto operator^=(const Derived& other) noexcept -> Derived&
        requires std::integral<typename Derived::value_type>
    {
        static_cast<Derived*>(this)->get() ^= other.get();
        return *static_cast<Derived*>(this);
    }

    [[nodiscard]]
    friend constexpr auto operator^(Derived lhs, const Derived& rhs) noexcept -> Derived
        requires std::integral<typename Derived::value_type>
    {
        lhs ^= rhs;
        return lhs;
    }
};

/** @brief Skill bundle: combines `BitwiseAndAssignable`, `BitwiseOrAssignable`, and `BitwiseXorAssignable`. */
template<typename Derived>
struct Bitwise : BitwiseAndAssignable<Derived>,
                 BitwiseOrAssignable<Derived>,
                 BitwiseXorAssignable<Derived>
{};

/** @brief Skill: adds `<=>` and `==` operators, enabling all six comparison operators. */
template<typename Derived>
struct Comparable {
    [[nodiscard]]
    friend constexpr auto operator<=>(const Derived& lhs, const Derived& rhs)
    {
        return lhs.get() <=> rhs.get();
    }

    [[nodiscard]]
    friend constexpr auto operator==(const Derived& lhs, const Derived& rhs) -> bool
    {
        return lhs.get() == rhs.get();
    }
};

/**
 * @brief Skill: exposes a `hash()` member for use in hash maps.
 *
 * Also enables `std::hash<NamedType<...>>` via the specialization at the
 * bottom of this header, so the type can be used directly as an
 * `unordered_map` key.
 */
template<typename Derived>
struct Hashable {
    [[nodiscard]]
    auto hash() const noexcept -> std::size_t
    {
        using HashType = typename Derived::value_type;
        return std::hash<HashType>{}(static_cast<const Derived*>(this)->get());
    }
};

/** @brief Skill: adds `operator<<` for use with any `std::ostream`. */
template<typename Derived>
struct Printable {
    friend auto operator<<(std::ostream& os, const Derived& obj) -> std::ostream&
    {
        return os << obj.get();
    }
};

template<typename T, FixedString Name, template <typename> class... Skills>
struct std::hash<NamedType<T, Name, Skills...>> {
    auto operator()(const NamedType<T, Name, Skills...>& obj) const noexcept -> std::size_t
    {
        return obj.hash();
    }
};

#endif // BLOG_COMMON_NAMED_TYPE_H