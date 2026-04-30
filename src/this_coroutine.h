#ifndef BLOG_THIS_COROUTINE_H
#define BLOG_THIS_COROUTINE_H

#include <memory>

#include "io_context.h"

namespace this_coroutine {

auto context() -> IOContext&
{
    // 每个线程都有一个IOContext实例，第一次调用时创建；后续调用返回同一个实例的引用
    thread_local auto context = std::make_unique<IOContext>();
    return *context;
}

} // namespace this_coroutine

#endif // BLOG_THIS_COROUTINE_H