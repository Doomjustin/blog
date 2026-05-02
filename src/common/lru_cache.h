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

    template<typename K>
    auto get(const K& key) noexcept -> std::optional<std::reference_wrapper<Value>>
    {
        auto it = cache_.find(key);
        if (it == cache_.end())
            return {};

        items_.splice(items_.begin(), items_, it->second);
        return std::ref(it->second->second);
    }

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