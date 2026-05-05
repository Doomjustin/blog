#include "this_coroutine.h"

#include <memory>

namespace async::this_coroutine {

namespace {

unsigned entries = 1024;

} // namespace


auto context() -> IOContext&
{
    // 每个线程都有一个IOContext实例，第一次调用时创建；后续调用返回同一个实例的引用
    thread_local auto context = std::make_unique<IOContext>(entries);
    return *context;
}

auto setup_buffer_ring(unsigned entries, unsigned size) -> unsigned
{
    return context().setup_buffer_ring(entries, size);
}

void setup_entries(unsigned new_entries)
{
    entries = new_entries;
}

} // namespace async::this_coroutine
