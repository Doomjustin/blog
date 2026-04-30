#ifndef BLOG_ASYNC_TRACKING_CONTEXT_H
#define BLOG_ASYNC_TRACKING_CONTEXT_H

namespace async {

/**
 * @brief Constrain execution contexts that can track outstanding work units.
 *
 * Contexts satisfying this concept expose a reference count of in-flight
 * operations so the event loop can decide when it is safe to exit.
 * `add_work()` is called when an async operation is enqueued and
 * `drop_work()` is called when it completes or is cancelled.
 */
template<typename T>
concept tracking_context = requires(T& ctx)
{
    ctx.add_work();
    ctx.drop_work();
};

} // namespace async

#endif // BLOG_ASYNC_TRACKING_CONTEXT_H