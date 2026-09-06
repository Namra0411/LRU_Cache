#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <mutex>
#include "../include/containers/lru_cache.hpp"
using namespace std;
using namespace std::chrono;

// Baseline: plain new/delete, no synchronization at all on the allocator.
// (LRUCache's own mutex still guards map/list correctness for all three
// variants below - only the allocation strategy differs between them.)
template <typename K, typename V>
class LRUCachePlain
{
    struct Node
    {
        K key; V value; Node *prev; Node *next;
        Node(K k, V v) : key(move(k)), value(move(v)), prev(nullptr), next(nullptr) {}
    };
    unordered_map<K, Node *> map;
    size_t capacity;
    Node *head, *tail;
    mutex mtx; // guards map/list, same as LRUCache does internally

    void remove(Node *n) {
        if (n->prev) n->prev->next = n->next;
        if (n->next) n->next->prev = n->prev;
        if (n == head) head = n->next;
        if (n == tail) tail = n->prev;
    }
    void insert_front(Node *n) {
        n->next = head; n->prev = nullptr;
        if (head) head->prev = n;
        head = n;
        if (!tail) tail = head;
    }

public:
    explicit LRUCachePlain(size_t cap) : capacity(cap), head(nullptr), tail(nullptr) {}

    V get(const K &key) {
        lock_guard<mutex> lock(mtx);
        auto it = map.find(key);
        if (it == map.end()) throw runtime_error("Key not found");
        Node *n = it->second;
        remove(n); insert_front(n);
        return n->value;
    }

    void put(K key, V value) {
        lock_guard<mutex> lock(mtx);
        auto it = map.find(key);
        if (it != map.end()) {
            it->second->value = move(value);
            remove(it->second); insert_front(it->second);
            return;
        }
        if (map.size() == capacity) {
            map.erase(tail->key);
            Node *old = tail;
            remove(old);
            delete old; // plain delete, no pool, no separate lock
        }
        Node *n = new Node(move(key), move(value)); // plain new
        insert_front(n);
        map[n->key] = n;
    }
};

template <typename Cache>
long long run_mt_workload(Cache &cache, int num_threads, int ops_per_thread)
{
    auto start = steady_clock::now();

    vector<thread> threads;
    for (int t = 0; t < num_threads; t++)
    {
        threads.emplace_back([&cache, t, ops_per_thread]()
        {
            for (int i = 0; i < ops_per_thread; i++)
            {
                int key = (t * 37 + i) % 2000; // overlapping key ranges -> real contention
                cache.put(key, i);
                try { cache.get(key); } catch (...) {}
            }
        });
    }
    for (auto &t : threads) t.join();

    auto end = steady_clock::now();
    return duration_cast<nanoseconds>(end - start).count();
}

int main()
{
    const int capacity = 500;
    const int total_ops = 2000000; // kept constant across thread counts

    for (int num_threads : {1, 2, 4, 8})
    {
        int ops_per_thread = total_ops / num_threads;

        long long pool_ns, plain_ns;

        {
            LRUCache<int, int> pooled(capacity);
            pool_ns = run_mt_workload(pooled, num_threads, ops_per_thread);
        }
        {
            LRUCachePlain<int, int> plain(capacity);
            plain_ns = run_mt_workload(plain, num_threads, ops_per_thread);
        }

        cout << "Threads: " << num_threads << "\n"
             << "  Pool allocator   : " << pool_ns / 1000000 << " ms total, "
             << pool_ns / total_ops << " ns/op avg\n"
             << "  Plain new/delete : " << plain_ns / 1000000 << " ms total, "
             << plain_ns / total_ops << " ns/op avg\n\n";
    }

    return 0;
}