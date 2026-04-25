#ifndef BLOG_OPERATION_H
#define BLOG_OPERATION_H

/**
 * @brief Base type for all io_uring completion callbacks.
 *
 * Each async I/O request stores a pointer to its owning `Operation` in the
 * SQE user-data field. When the corresponding CQE arrives, the event loop
 * calls `complete` to deliver the result and resume the waiting coroutine.
 */
#include <coroutine>
#include <cstdint>
#include <utility>
struct Operation {
    virtual ~Operation() = default;

    /**
     * @brief Deliver io_uring completion result to the owning coroutine.
     *
     * @param res Completion result. Negative values represent negated `errno`.
     * @param flags CQE flags from io_uring.
     */
    virtual void complete(int res, unsigned flags) = 0;
};


struct CancelableOperation : public Operation {
    CancelableOperation* parent{ nullptr };
    
    CancelableOperation() = default;
    
    virtual ~CancelableOperation() = default;
    
    void resume(std::coroutine_handle<> handle, int result, std::uint32_t flags) noexcept
    {
        if (parent) {
            parent->complete(result, flags);
        }
        else if (handle) {
            auto h = std::exchange(handle, nullptr);
            h.resume();
        }
    }
};

#endif // BLOG_OPERATION_H