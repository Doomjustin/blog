#include "this_coroutine.h"

#include <memory>

namespace async::this_coroutine {

auto context() -> IOContext&
{
    // 每个线程都有一个IOContext实例，第一次调用时创建；后续调用返回同一个实例的引用
    thread_local auto context = std::make_unique<IOContext>(detail::entries);
    return *context;
}

} // namespace async::this_coroutine
