#ifndef BLOG_COMMON_LRU_CACHE_H
#define BLOG_COMMON_LRU_CACHE_H

#include <cassert>
#include <functional>
#include <list>
#include <memory_resource>
#include <optional>
#include <unordered_map>
#include <utility>

#include <gsl/gsl>

/**
 * @brief Fixed-capacity LRU (Least Recently Used) cache backed by PMR allocators.
 *
 * Stores up to `capacity` key-value pairs. When a new entry would exceed
 * capacity, the least recently used entry is evicted. Both `put` and `get`
 * count as a "use" and promote the accessed entry to the MRU position.
 *
 * The backing storage is a `std::pmr::list` (O(1) splice for promotion)
 * indexed by a `std::pmr::unordered_map` for O(1) lookups. All allocations
 * go through the supplied `memory_resource`, defaulting to
 * `std::pmr::get_default_resource()`.
 *
 * @tparam Key       Key type. Must be hashable by `KeyHash` and comparable by `KeyEqual`.
 * @tparam Value     Value type. Must be move-constructible.
 * @tparam KeyHash   Hash functor; defaults to `std::hash<Key>`.
 * @tparam KeyEqual  Equality functor; defaults to `std::equal_to<Key>`.
 *
 * @pre `capacity > 0`
 *
 * Example:
 * @code
 * LRUCache<std::string, int, StringHash, std::equal_to<>> cache{ 3 };
 * cache.put("a", 1);
 * auto val = cache.get("a");  // std::optional<std::reference_wrapper<int>>
 * @endcode
 */
template<typename Key,
         typename Value, 
         typename KeyHash = std::hash<Key>, 
         typename KeyEqual = std::equal_to<Key>>
class LRUCache {
public:
    using resource = std::pmr::memory_resource;
    using size_type = std::size_t;
    using value_type = std::pair<const Key, Value>;
    using container = std::pmr::list<value_type>;
    using iterator = typename container::iterator;
    using const_iterator = typename container::const_iterator;
    using cache = std::pmr::unordered_map<Key, iterator, KeyHash, KeyEqual>;

    /**
     * @brief Constructs the cache with the given capacity and memory resource.
     *
     * @param capacity        Maximum number of entries to hold. Must be > 0.
     * @param memory_resource PMR allocator to use for all internal storage.
     *
     * @pre `capacity > 0`
     */
    explicit LRUCache(size_type capacity, gsl::not_null<resource*> memory_resource = std::pmr::get_default_resource())
      : capacity_{ capacity },
        items_{ memory_resource.get() },
        cache_{ memory_resource.get() }
    {
        assert(capacity_ > 0);
    }

    LRUCache(const LRUCache&) = delete;
    auto operator=(const LRUCache&) -> LRUCache& = delete;

    LRUCache(LRUCache&&) = default;
    auto operator=(LRUCache&&) -> LRUCache& = default;

    ~LRUCache() = default;

    /**
     * @brief Inserts or updates a key-value pair, promoting the entry to MRU.
     *
     * If `key` already exists, its value is overwritten and the entry is
     * moved to the most recently used position. If the cache is full, the
     * least recently used entry is evicted before insertion.
     *
     * @param key   Key to insert or update.
     * @param value Value to associate with the key.
     */
    template<typename K, typename V>
    requires std::constructible_from<Key, K>
          && std::constructible_from<Value, V>
    void put(K&& key, V&& value)
    {
        if (capacity_ == 0) return; 

        auto it = cache_.find(key);
        if (it != cache_.end()) {
            it->second->second = std::forward<V>(value);
            items_.splice(items_.begin(), items_, it->second);
            return;
        }

        if (cache_.size() == capacity_) {
            cache_.erase(items_.back().first);
            items_.pop_back();
        }

        items_.emplace_front(std::forward<K>(key), std::forward<V>(value));
        cache_.emplace(items_.begin()->first, items_.begin());
    }

    /**
     * @brief Looks up a key and promotes it to the MRU position on hit.
     *
     * @param key  Key to look up. Supports heterogeneous lookup if `KeyHash`
     *             and `KeyEqual` are transparent.
     * @return A reference wrapper to the value on hit, or an empty optional on miss.
     *         The reference remains valid until the entry is evicted.
     */
    template<typename K>
    auto get(const K& key) noexcept -> std::optional<std::reference_wrapper<Value>>
    {
        auto it = cache_.find(key);
        if (it == cache_.end())
            return {};

        items_.splice(items_.begin(), items_, it->second);
        return std::ref(it->second->second);
    }

    /** @brief Removes all entries, leaving the cache empty. */
    void clear() noexcept
    {
        items_.clear();
        cache_.clear();
    }

    [[nodiscard]]
    constexpr auto capacity() const noexcept -> size_type
    {
        return capacity_;
    }

    [[nodiscard]]
    constexpr auto size() const noexcept -> size_type
    {
        return items_.size();
    }

    auto begin() noexcept -> iterator { return items_.begin(); }
    auto end() noexcept -> iterator { return items_.end(); }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return items_.begin(); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return items_.end(); }

    auto cbegin() noexcept -> const_iterator { return items_.cbegin(); }
    auto cend() noexcept -> const_iterator { return items_.cend(); }
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return items_.cbegin(); }
    [[nodiscard]] auto cend() const noexcept -> const_iterator { return items_.cend(); }

private:
    size_type capacity_;
    container items_;
    cache cache_;
};

#endif // BLOG_COMMON_LRU_CACHE_H