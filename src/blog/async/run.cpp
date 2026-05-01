#include "run.h"

#include <algorithm>

#include "this_coroutine.h"

namespace async {

namespace detail {

void push(IOContext& context)
{
    std::scoped_lock locker{ contexts_mutex };
    active_contexts.push_back(&context);
}

void erase(IOContext& context)
{
    std::scoped_lock locker{ contexts_mutex };
    std::erase_if(active_contexts, [&context](IOContext* ctx) { return ctx == &context; });
}

} // namespace detail

auto setup_buffer_ring(unsigned entries, unsigned size) -> unsigned
{
    return this_coroutine::context().setup_buffer_ring(entries, size);
}

void setup_entries(unsigned entries)
{
    this_coroutine::detail::entries = entries;
}

void stop()
{
    std::scoped_lock lock{ detail::contexts_mutex };
    std::ranges::for_each(detail::active_contexts, 
        [](IOContext* context) -> void
        {
            context->stop();
        }
    );

    // 每个context在被stop之后都会从active_contexts里把自己删除，所以这里不需要clear
}

} // namespace async 