#include "this_coroutine.h"

namespace async::this_coroutine {

namespace {

unsigned entries = 1024;

thread_local IOContext* bound_context = nullptr;

auto thread_context() -> IOContext&
{
    thread_local IOContext ctx{ entries };
    return ctx;
}

} // namespace


auto context() -> IOContext&
{
    if (bound_context)
        return *bound_context;
    
    // 没有绑定上下文时，返回当前线程的默认 context。
    return thread_context();
}

auto setup_buffer_ring(unsigned entries, unsigned size) -> unsigned
{
    return context().setup_buffer_ring(entries, size);
}

void setup_entries(unsigned new_entries)
{
    entries = new_entries;
}

ContextBinder::ContextBinder() noexcept
{
    context_ = &thread_context();
    previous_ = bound_context;
    bound_context = context_;
}

ContextBinder::ContextBinder(IOContext& ctx) noexcept
  : context_{ &ctx }, previous_{ bound_context }
{
    bound_context = &ctx;
}

ContextBinder::~ContextBinder() noexcept
{
    bound_context = previous_;
}

} // namespace async::this_coroutine
