#pragma once
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <mutex>
using namespace std;
#include "../allocator/pool_allocator.hpp"

template <typename K, typename V>
class LRUCache
{
    struct Node
    {
        K key;
        V value;
        Node *prev;
        Node *next;

        Node(K k, V v)
            : key(move(k)), value(move(v)), prev(nullptr), next(nullptr) {}
    };

    using Alloc = PoolAllocator<Node>;
    unordered_map<K, Node *> map;
    size_t capacity;
    Node *head; // most recently used
    Node *tail; // least recently used
    Alloc alloc;
    mutable mutex mtx; // protects map, head, tail, and all node links

     // Remove node from list
    void remove(Node *node)
    {
        if (node->prev)
            node->prev->next = node->next;
        if (node->next)
            node->next->prev = node->prev;
        if (node == head)
            head = node->next;
        if (node == tail)
            tail = node->prev;
    }

     // Insert node at front
    void insert_front(Node *node)
    {
        node->next = head;
        node->prev = nullptr;
        if (head)
            head->prev = node;
        head = node;
        if (!tail)
            tail = head;
    }

public:
    explicit LRUCache(size_t cap) : capacity(cap), head(nullptr), tail(nullptr) {}

    
    V get(const K &key)
    {
        lock_guard<mutex> lock(mtx);

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
        lock_guard<mutex> lock(mtx);

        auto it = map.find(key);
        if (it != map.end())
        {
            Node *node = it->second;
            node->value = move(value);
            remove(node);
            insert_front(node);
            return;
        }

         // Cache full → evict LRU
        if (map.size() == capacity)
        {
            map.erase(tail->key);
            Node *old = tail;
            remove(old);

            old->~Node();
            alloc.deallocate(old, 1);
        }

        Node *mem = alloc.allocate(1);
        Node *node = new (mem) Node(move(key), move(value));

        insert_front(node);
        map[node->key] = node;
    }
};