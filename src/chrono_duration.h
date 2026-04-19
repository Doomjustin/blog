#ifndef BLOG_CHRONO_DURATION_H
#define BLOG_CHRONO_DURATION_H

#include <chrono>
#include <type_traits>

template<typename T>
struct is_chrono_duration_impl: std::false_type {};

template<typename Rep, typename Period>
struct is_chrono_duration_impl<std::chrono::duration<Rep, Period>>: std::true_type {};

template<typename T>
concept chrono_duration = is_chrono_duration_impl<std::remove_cvref_t<T>>::value;

#endif // BLOG_CHRONO_DURATION_H