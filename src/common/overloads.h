#ifndef BLOG_COMMON_OVERLOADS_H
#define BLOG_COMMON_OVERLOADS_H

template<typename... T>
struct Overload : T... {
    using T::operator()...;
};

template<typename... T>
Overload(T...) -> Overload<T...>;

#endif // BLOG_COMMON_OVERLOADS_H