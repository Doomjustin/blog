#ifndef BLOG_COMMON_CHRONO_DURATION_H
#define BLOG_COMMON_CHRONO_DURATION_H

#include <chrono>
#include <type_traits>

/**
 * @brief Primary template: T is not a `std::chrono::duration`.
 */
template<typename T>
struct is_chrono_duration_impl: std::false_type {};

/**
 * @brief Partial specialization that matches any `std::chrono::duration` instantiation.
 */
template<typename Rep, typename Period>
struct is_chrono_duration_impl<std::chrono::duration<Rep, Period>>: std::true_type {};

/**
 * @brief Constrain template parameters to `std::chrono::duration` specializations.
 *
 * This concept is used by timer and timeout APIs to reject non-duration
 * arguments at compile time rather than silently casting.
 */
template<typename T>
concept chrono_duration = is_chrono_duration_impl<std::remove_cvref_t<T>>::value;

#endif // BLOG_COMMON_CHRONO_DURATION_H