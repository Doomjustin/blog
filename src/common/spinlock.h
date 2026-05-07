#ifndef BLOG_COMMON_SPINLOCK_H
#define BLOG_COMMON_SPINLOCK_H

#include <atomic>
#include <thread>

#if defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h>
#  define BLOG_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
#  define BLOG_CPU_RELAX() __asm__ volatile("yield" ::: "memory")
#else
#  define BLOG_CPU_RELAX() std::this_thread::yield()
#endif

/**
 * @brief Spinlock for very short critical sections (< ~50 ns hold time).
 *
 * Suitable when the critical section contains only in-memory operations and
 * no system calls. Uses exponential back-off: CPU pause for the first 16
 * spins, then yields the time-slice.
 *
 * @note Do NOT hold this lock while calling any function that may invoke a
 *       system call (e.g. `io_uring`, `write`, `futex`). Use `std::mutex`
 *       for those cases.
 */
class SpinLock {
public:
    void lock() noexcept
    {
        for (int spin = 0; flag_.test_and_set(std::memory_order_acquire); ++spin) {
            if (spin < 16)
                BLOG_CPU_RELAX();
            else
                std::this_thread::yield();
        }
    }

    void unlock() noexcept 
    { 
        flag_.clear(std::memory_order_release); 
    }

    [[nodiscard]]
    auto try_lock() noexcept -> bool 
    { 
        return !flag_.test_and_set(std::memory_order_acquire); 
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

#undef BLOG_CPU_RELAX

#endif // BLOG_COMMON_SPINLOCK_H
