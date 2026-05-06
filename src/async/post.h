#ifndef BLOG_ASYNC_POST_H
#define BLOG_ASYNC_POST_H

#include <io_context.h>
#include <operation.h>

namespace async {

template<typename Func>
class DispatchOperation: public Operation {
public:
    DispatchOperation(IOContext& context, Func&& f, bool always = false)
      : context_{ &context }
      , func_{ std::forward<Func>(f) }
      , always_{ always }
    {}

    void complete(int result, std::uint32_t flags) noexcept override
    {
        context_->drop_work();

        if (always_ || result != -ECANCELED)
            func_();

        delete this;
    }

private:
    IOContext* context_;
    Func func_;
    bool always_;
};

template<typename Func>
void post(IOContext& context, Func&& f, bool always = false)
{
    // 强制堆分配以确保操作对象在 complete() 中仍然有效
    context.add_work();
    auto* op = new DispatchOperation<Func>{ context, std::forward<Func>(f), always };
    context.post(op);
}

template<typename Func>
void dispatch(IOContext& context, Func&& func)
{
    context.add_work();
    auto* op = new DispatchOperation<Func>{ context, std::forward<Func>(func) };
    if (context.is_owner_thread())
        context.submit(op);
    else
        context.post(op);
}

} // namespace async

#endif // BLOG_ASYNC_POST_H