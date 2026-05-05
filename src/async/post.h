#ifndef BLOG_ASYNC_POST_H
#define BLOG_ASYNC_POST_H

#include "io_context.h"

#include <operation.h>

namespace async {

template<typename Func>
class DispatchOperation: public Operation {
public:
    DispatchOperation(Func&& f)
      : func_{ std::forward<Func>(f) }
    {}

    void complete(int result, std::uint32_t flags) noexcept override
    {
        func_();
        delete this;
    }

private:
    Func func_;
};

template<typename Func>
void post(IOContext& context, Func&& f)
{
    // 强制堆分配以确保操作对象在 complete() 中仍然有效
    auto* op = new DispatchOperation<Func>{ std::forward<Func>(f) };
    context.post(op);
}

template<typename Func>
void dispatch(IOContext& context, Func&& func)
{
    auto* op = new DispatchOperation<Func>{ std::forward<Func>(func) };
    if (context.is_owner_thread())
        context.submit(op);
    else
        context.post(op);
}

} // namespace async

#endif // BLOG_ASYNC_POST_H