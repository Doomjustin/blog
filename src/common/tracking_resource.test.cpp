#include "tracking_resource.h"

#include <cstddef>
#include <memory_resource>
#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("TrackingMemoryResource: starts with zero usage", "[tracking_resource]")
{
    TrackingMemoryResource tracking{ std::pmr::new_delete_resource() };
    REQUIRE(tracking.used_memory() == 0U);
}

TEST_CASE("TrackingMemoryResource: tracks allocate and deallocate", "[tracking_resource]")
{
    TrackingMemoryResource tracking{ std::pmr::new_delete_resource() };

    constexpr std::size_t bytes = 64U;
    void* ptr = tracking.allocate(bytes, alignof(std::max_align_t));

    REQUIRE(tracking.used_memory() == bytes);

    tracking.deallocate(ptr, bytes, alignof(std::max_align_t));
    REQUIRE(tracking.used_memory() == 0U);
}

TEST_CASE("TrackingMemoryResource: accumulates multiple allocations", "[tracking_resource]")
{
    TrackingMemoryResource tracking{ std::pmr::new_delete_resource() };

    constexpr std::size_t first = 32U;
    constexpr std::size_t second = 96U;

    void* p1 = tracking.allocate(first, alignof(std::max_align_t));
    void* p2 = tracking.allocate(second, alignof(std::max_align_t));

    REQUIRE(tracking.used_memory() == first + second);

    tracking.deallocate(p2, second, alignof(std::max_align_t));
    REQUIRE(tracking.used_memory() == first);

    tracking.deallocate(p1, first, alignof(std::max_align_t));
    REQUIRE(tracking.used_memory() == 0U);
}

TEST_CASE("TrackingMemoryResource: works with pmr container lifecycle", "[tracking_resource]")
{
    TrackingMemoryResource tracking{ std::pmr::new_delete_resource() };

    {
        std::pmr::vector<int> values{ &tracking };
        values.reserve(128);
        values.assign(128, 42);

        REQUIRE(tracking.used_memory() > 0U);
    }

    REQUIRE(tracking.used_memory() == 0U);
}

TEST_CASE("TrackingMemoryResource: equality is identity-based", "[tracking_resource]")
{
    TrackingMemoryResource left{ std::pmr::new_delete_resource() };
    TrackingMemoryResource right{ std::pmr::new_delete_resource() };

    REQUIRE(left.is_equal(left));
    REQUIRE_FALSE(left.is_equal(right));
}
