#ifndef BLOG_COMMON_TRACKING_RESOURCE_H
#define BLOG_COMMON_TRACKING_RESOURCE_H

#include <atomic>
#include <cstddef>
#include <memory_resource>

#include <gsl/gsl>

class TrackingMemoryResource: public std::pmr::memory_resource {
public:
    explicit TrackingMemoryResource(gsl::not_null<std::pmr::memory_resource*> upstream)
      : upstream_{ upstream }
    {}

    ~TrackingMemoryResource() = default;

    [[nodiscard]]
    auto used_memory() const noexcept -> std::size_t
    {
        return total_allocated_.load(std::memory_order_relaxed);
    }

private:
    std::pmr::memory_resource* upstream_;
    std::atomic<std::size_t> total_allocated_{ 0 };

    auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override;

    void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;

    [[nodiscard]]
    constexpr auto do_is_equal(const std::pmr::memory_resource& other) const noexcept -> bool override
    {
        return this == &other;
    }
};

#endif // BLOG_COMMON_TRACKING_RESOURCE_H