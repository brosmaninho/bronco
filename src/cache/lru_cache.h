#pragma once

#include <list>
#include <unordered_map>
#include <optional>
#include <shared_mutex>
#include <cstddef>

namespace bronco::cache {

/// Thread-safe LRU (Least Recently Used) cache with O(1) get and put operations.
///
/// Uses a doubly-linked list to maintain access order and a hash map for
/// constant-time key lookup. The most recently accessed item is at the front
/// of the list; eviction removes from the back.
///
/// Read operations (contains, size) use a shared lock to allow concurrent
/// readers. Write operations (get, put, erase, clear) use an exclusive lock.
/// Note: get() is a write operation because it updates access order.
///
/// @tparam Key The key type (must be hashable)
/// @tparam Value The value type
template <typename Key, typename Value>
class LruCache {
public:
    using KeyValue = std::pair<Key, Value>;
    using ListIterator = typename std::list<KeyValue>::iterator;

    /// Construct an LRU cache with the given maximum capacity.
    /// @param capacity Maximum number of entries before eviction
    explicit LruCache(std::size_t capacity = 1000)
        : m_capacity(capacity)
    {
    }

    /// Look up a value by key. Returns std::nullopt if not found.
    /// Moves the accessed item to the front (most recently used).
    /// Uses exclusive lock since it mutates the list order.
    std::optional<Value> get(const Key& key)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_map.find(key);
        if (it == m_map.end())
        {
            return std::nullopt;
        }

        // Move to front (most recently used)
        m_list.splice(m_list.begin(), m_list, it->second);
        return it->second->second;
    }

    /// Look up a value without updating access order.
    /// Uses shared lock - multiple threads can peek concurrently.
    std::optional<Value> peek(const Key& key) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_map.find(key);
        if (it == m_map.end())
        {
            return std::nullopt;
        }

        return it->second->second;
    }

    /// Insert or update a key-value pair.
    /// If the cache is at capacity, the least recently used item is evicted.
    void put(const Key& key, const Value& value)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_map.find(key);
        if (it != m_map.end())
        {
            // Key exists - update value and move to front
            it->second->second = value;
            m_list.splice(m_list.begin(), m_list, it->second);
            return;
        }

        // Evict least recently used if at capacity
        if (m_map.size() >= m_capacity)
        {
            auto& back = m_list.back();
            m_map.erase(back.first);
            m_list.pop_back();
        }

        // Insert at front
        m_list.emplace_front(key, value);
        m_map[key] = m_list.begin();
    }

    /// Check if a key exists in the cache (does NOT update access order).
    /// Uses shared lock for concurrent reads.
    bool contains(const Key& key) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_map.find(key) != m_map.end();
    }

    /// Remove a specific key from the cache.
    /// @return true if the key was found and removed
    bool erase(const Key& key)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        auto it = m_map.find(key);
        if (it == m_map.end()) return false;

        m_list.erase(it->second);
        m_map.erase(it);
        return true;
    }

    /// Clear all entries from the cache.
    void clear()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_list.clear();
        m_map.clear();
    }

    /// Get the current number of entries in the cache.
    std::size_t size() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_map.size();
    }

    /// Get the maximum capacity of the cache.
    std::size_t capacity() const
    {
        return m_capacity;
    }

    /// Resize the cache capacity. If new capacity is smaller,
    /// evicts least recently used items until within bounds.
    void resize(std::size_t newCapacity)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_capacity = newCapacity;

        while (m_map.size() > m_capacity)
        {
            auto& back = m_list.back();
            m_map.erase(back.first);
            m_list.pop_back();
        }
    }

private:
    std::size_t m_capacity;
    std::list<KeyValue> m_list;
    std::unordered_map<Key, ListIterator> m_map;
    mutable std::shared_mutex m_mutex;
};

} // namespace bronco::cache
