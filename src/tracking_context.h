#ifndef BLOG_TRACKING_CONTEXT_H
#define BLOG_TRACKING_CONTEXT_H

template<typename T>
concept tracking_context = requires(T& ctx)
{
    ctx.add_work();
    ctx.drop_work();
};

#endif // BLOG_TRACKING_CONTEXT_H