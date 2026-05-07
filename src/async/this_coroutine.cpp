#include "this_coroutine.h"

#include <memory>

namespace async::this_coroutine {

namespace {

unsigned entries = 1024;

thread_local IOContext* bound_context = nullptr;

} // namespace


auto context() -> IOContext&
{
    if (bound_context)
        return *bound_context;
    
    // 没有绑定的上下文，创建一个线程专用的IOContext并绑定
    // 但是这个context不会被实际使用，在context::run的时候，会被ContextBinder替换掉
    thread_local auto ctx = std::make_unique<IOContext>(entries);
    return *ctx;
}

ContextBinder::ContextBinder() noexcept
{
    thread_local auto ctx = std::make_unique<IOContext>(entries);
    context_ = ctx.get();
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

auto setup_buffer_ring(unsigned entries, unsigned size) -> unsigned
{
    return context().setup_buffer_ring(entries, size);
}

void setup_entries(unsigned new_entries)
{
    entries = new_entries;
}

} // namespace async::this_coroutine
