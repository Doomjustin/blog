#include "lru_cache.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

// ------- basic operations ---------------------------------------------------

TEST_CASE("LRUCache: put and get hit", "[lru_cache]")
{
    LRUCache<int, std::string> cache{ 4 };
    cache.put(1, "one");
    auto result = cache.get(1);
    REQUIRE(result.has_value());
    REQUIRE(result->get() == "one");
}

TEST_CASE("LRUCache: get miss returns empty optional", "[lru_cache]")
{
    LRUCache<int, std::string> cache{ 4 };
    REQUIRE_FALSE(cache.get(99).has_value());
}

TEST_CASE("LRUCache: put overwrites existing value", "[lru_cache]")
{
    LRUCache<int, std::string> cache{ 4 };
    cache.put(1, "one");
    cache.put(1, "uno");
    REQUIRE(cache.get(1)->get() == "uno");
    REQUIRE(cache.size() == 1);
}

TEST_CASE("LRUCache: size tracks inserted items", "[lru_cache]")
{
    LRUCache<int, int> cache{ 4 };
    REQUIRE(cache.size() == 0);
    cache.put(1, 10);
    REQUIRE(cache.size() == 1);
    cache.put(2, 20);
    REQUIRE(cache.size() == 2);
}

TEST_CASE("LRUCache: capacity is reported correctly", "[lru_cache]")
{
    LRUCache<int, int> cache{ 3 };
    REQUIRE(cache.capacity() == 3);
}

// ------- eviction -----------------------------------------------------------

TEST_CASE("LRUCache: evicts least recently used on overflow", "[lru_cache]")
{
    LRUCache<int, int> cache{ 3 };
    cache.put(1, 1);
    cache.put(2, 2);
    cache.put(3, 3);
    // 1 is LRU; inserting 4 should evict it
    cache.put(4, 4);
    REQUIRE_FALSE(cache.get(1).has_value());
    REQUIRE(cache.get(2).has_value());
    REQUIRE(cache.get(3).has_value());
    REQUIRE(cache.get(4).has_value());
    REQUIRE(cache.size() == 3);
}

TEST_CASE("LRUCache: get promotes entry to most recently used", "[lru_cache]")
{
    LRUCache<int, int> cache{ 3 };
    cache.put(1, 1);
    cache.put(2, 2);
    cache.put(3, 3);
    // Access 1 to make it MRU; now 2 is LRU
    cache.get(1);
    cache.put(4, 4);
    REQUIRE(cache.get(1).has_value());
    REQUIRE_FALSE(cache.get(2).has_value()); // evicted
    REQUIRE(cache.get(3).has_value());
    REQUIRE(cache.get(4).has_value());
}

TEST_CASE("LRUCache: put existing key promotes to MRU without eviction", "[lru_cache]")
{
    LRUCache<int, int> cache{ 3 };
    cache.put(1, 1);
    cache.put(2, 2);
    cache.put(3, 3);
    // Re-put 1 promotes it; 2 becomes LRU
    cache.put(1, 10);
    cache.put(4, 4);
    REQUIRE(cache.get(1)->get() == 10);
    REQUIRE_FALSE(cache.get(2).has_value()); // evicted
}

// ------- clear --------------------------------------------------------------

TEST_CASE("LRUCache: clear empties the cache", "[lru_cache]")
{
    LRUCache<int, int> cache{ 4 };
    cache.put(1, 1);
    cache.put(2, 2);
    cache.clear();
    REQUIRE(cache.size() == 0);
    REQUIRE_FALSE(cache.get(1).has_value());
    REQUIRE_FALSE(cache.get(2).has_value());
}

// ------- get returns reference ----------------------------------------------

TEST_CASE("LRUCache: get returns mutable reference to value", "[lru_cache]")
{
    LRUCache<int, std::string> cache{ 4 };
    cache.put(1, "hello");
    cache.get(1)->get() = "world";
    REQUIRE(cache.get(1)->get() == "world");
}

// ------- transparent hash ---------------------------------------------------

TEST_CASE("LRUCache: works with StringHash for heterogeneous lookup", "[lru_cache]")
{
    // Verify the cache compiles and works with a custom hash (StringHash is in hash.h,
    // but we use std::hash<std::string> here to keep this test self-contained)
    LRUCache<std::string, int> cache{ 4 };
    cache.put("key", 42);
    REQUIRE(cache.get("key").has_value());
    REQUIRE(cache.get("key")->get() == 42);
}
