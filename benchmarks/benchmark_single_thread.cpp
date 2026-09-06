#include <iostream>
#include <chrono>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <mutex>
#include "../include/containers/lru_cache.hpp"
using namespace std;
using namespace std::chrono;

// Baseline cache: identical logic to LRUCache, but every node goes through
// plain new/delete instead of the pool allocator.
template <typename K, typename V>
class LRUCachePlain
{
    struct Node
    {
        K key;
        V value;
        Node *prev;
        Node *next;
        Node(K k, V v) : key(move(k)), value(move(v)), prev(nullptr), next(nullptr) {}
    };

    unordered_map<K, Node *> map;
    size_t capacity;
    Node *head;
    Node *tail;

    void remove(Node *node)
    {
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        if (node == head) head = node->next;
        if (node == tail) tail = node->prev;
    }

    void insert_front(Node *node)
    {
        node->next = head;
        node->prev = nullptr;
        if (head) head->prev = node;
        head = node;
        if (!tail) tail = head;
    }

public:
    explicit LRUCachePlain(size_t cap) : capacity(cap), head(nullptr), tail(nullptr) {}

    V get(const K &key)
    {
        auto it = map.find(key);
        if (it == map.end())
            throw runtime_error("Key not found");
        Node *node = it->second;
        remove(node);
        insert_front(node);
        return node->value;
    }

    void put(K key, V value)
    {
        auto it = map.find(key);
        if (it != map.end())
        {
            Node *node = it->second;
            node->value = move(value);
            remove(node);
            insert_front(node);
            return;
        }
        if (map.size() == capacity)
        {
            map.erase(tail->key);
            Node *old = tail;
            remove(old);
            delete old;
        }
        Node *node = new Node(move(key), move(value));
        insert_front(node);
        map[node->key] = node;
    }
};

// Same as LRUCachePlain, but every new/delete is wrapped in a mutex lock,
// to isolate whether the pool's slowdown is the mutex itself or something
// else about the pool's internals.
template <typename K, typename V>
class LRUCacheLockedPlain
{
    struct Node
    {
        K key;
        V value;
        Node *prev;
        Node *next;
        Node(K k, V v) : key(move(k)), value(move(v)), prev(nullptr), next(nullptr) {}
    };

    unordered_map<K, Node *> map;
    size_t capacity;
    Node *head;
    Node *tail;
    mutex alloc_mtx; // guards only the new/delete calls, mirroring MemoryPool's lock

    void remove(Node *node)
    {
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
        if (node == head) head = node->next;
        if (node == tail) tail = node->prev;
    }

    void insert_front(Node *node)
    {
        node->next = head;
        node->prev = nullptr;
        if (head) head->prev = node;
        head = node;
        if (!tail) tail = head;
    }

public:
    explicit LRUCacheLockedPlain(size_t cap) : capacity(cap), head(nullptr), tail(nullptr) {}

    V get(const K &key)
    {
        auto it = map.find(key);
        if (it == map.end())
            throw runtime_error("Key not found");
        Node *node = it->second;
        remove(node);
        insert_front(node);
        return node->value;
    }

    void put(K key, V value)
    {
        auto it = map.find(key);
        if (it != map.end())
        {
            Node *node = it->second;
            node->value = move(value);
            remove(node);
            insert_front(node);
            return;
        }
        if (map.size() == capacity)
        {
            map.erase(tail->key);
            Node *old = tail;
            remove(old);

            lock_guard<mutex> lock(alloc_mtx);
            delete old;
        }

        Node *node;
        {
            lock_guard<mutex> lock(alloc_mtx);
            node = new Node(move(key), move(value));
        }
        insert_front(node);
        map[node->key] = node;
    }
};

// Runs put/get churn: capacity is small relative to key range, so this
// forces continuous eviction on every cache, which is exactly the
// allocation pattern the pool is meant to help with.
template <typename Cache>
long long run_workload(Cache &cache, int ops)
{
    auto start = steady_clock::now();

    for (int i = 0; i < ops; i++)
    {
        int key = i % 2000; // key range >> capacity forces eviction churn
        cache.put(key, i);
        try { cache.get(key); } catch (...) {}
    }

    auto end = steady_clock::now();
    return duration_cast<nanoseconds>(end - start).count();
}

int main()
{
    const int capacity = 500;
    const int ops = 2000000;

    {
        LRUCache<int, int> pooled(capacity);
        long long ns = run_workload(pooled, ops);
        cout << "Pool allocator      : " << ns / 1000000 << " ms total, "
             << ns / ops << " ns/op avg\n";
    }

    {
        LRUCachePlain<int, int> plain(capacity);
        long long ns = run_workload(plain, ops);
        cout << "Plain new/delete    : " << ns / 1000000 << " ms total, "
             << ns / ops << " ns/op avg\n";
    }

    {
        LRUCacheLockedPlain<int, int> lockedPlain(capacity);
        long long ns = run_workload(lockedPlain, ops);
        cout << "Locked new/delete   : " << ns / 1000000 << " ms total, "
             << ns / ops << " ns/op avg\n";
    }

    return 0;
}