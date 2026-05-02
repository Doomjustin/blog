#ifndef BLOG_COMMON_OVERLOADS_H
#define BLOG_COMMON_OVERLOADS_H

/**
 * @brief Aggregate multiple callable types into one overload set.
 *
 * Combine lambdas or function objects as `std::visit` visitors without
 * manual virtual dispatch:
 * @code
 * std::visit(Overload{
 *     [](int v)  { ... },
 *     [](float v){ ... },
 * }, variant);
 * @endcode
 */
template<typename... T>
struct Overload: T... {
    using T::operator()...;
};

/** @brief CTAD deduction guide for `Overload`. */
template<typename... T>
Overload(T...) -> Overload<T...>;

#endif // BLOG_COMMON_OVERLOADS_H