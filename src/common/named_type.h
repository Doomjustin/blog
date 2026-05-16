#ifndef BLOG_COMMON_NAMED_TYPE_H
#define BLOG_COMMON_NAMED_TYPE_H

#include <cmath>
#include <concepts>
#include <cstddef>

#include <common/fixed_string.h>

template<typename T, FixedString Name, template<typename> class... Skills>
    requires std::is_arithmetic_v<T>
class NamedType : public Skills<NamedType<T, Name, Skills...>>... {
public:
    using value_type = T;

    explicit constexpr NamedType(const T& v) noexcept
      : value_{ v }
    {}

    explicit constexpr NamedType(T&& v) noexcept
      : value_{ std::move(v) }
    {}

    [[nodiscard]]
    constexpr auto get() noexcept -> T&
    {
        return value_;
    }

    [[nodiscard]]
    constexpr auto get() const noexcept -> const T&
    {
        return value_;
    }

    [[nodiscard]]
    auto operator*() noexcept -> T&
    {
        return value_;
    }

    [[nodiscard]]
    auto operator*() const noexcept -> const T&
    {
        return value_;
    }

private:
    T value_;
};

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
        static_cast<Derived*>(this)->get() =
            std::fmod(static_cast<Derived*>(this)->get(), other.get());
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

template<typename Derived>
struct Arithmetic
  : Decrementable<Derived>
  , Incrementable<Derived>
  , Addable<Derived>
  , Subtractable<Derived>
  , Multipliable<Derived>
  , Dividable<Derived>
  , RemainderAssignable<Derived> {};

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

template<typename Derived>
struct Bitwise
  : BitwiseAndAssignable<Derived>
  , BitwiseOrAssignable<Derived>
  , BitwiseXorAssignable<Derived> {};

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

template<typename Derived>
struct Hashable {
    [[nodiscard]]
    auto hash() const noexcept -> std::size_t
    {
        using HashType = typename Derived::value_type;
        return std::hash<HashType>{}(static_cast<const Derived*>(this)->get());
    }
};

template<typename Derived>
struct Printable {
    friend auto operator<<(std::ostream& os, const Derived& obj) -> std::ostream&
    {
        return os << obj.get();
    }
};

template<typename T, FixedString Name, template<typename> class... Skills>
struct std::hash<NamedType<T, Name, Skills...>> {
    auto operator()(const NamedType<T, Name, Skills...>& obj) const noexcept -> std::size_t
    {
        return obj.hash();
    }
};

#endif // BLOG_COMMON_NAMED_TYPE_H