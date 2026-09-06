#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include "../include/allocator/pool_allocator.hpp"
using namespace std;
using namespace std::chrono;

struct Payload
{
    int a, b, c, d;
};

// --- Variant 1: your actual pool allocator ---
struct PoolWrapper
{
    PoolAllocator<Payload> alloc;
    Payload *allocate() { return alloc.allocate(1); }
    void deallocate(Payload *p) { alloc.deallocate(p, 1); }
};

// --- Variant 2: plain new/delete, wrapped in a single shared mutex ---
struct LockedPlainWrapper
{
    mutex m;
    Payload *allocate()
    {
        lock_guard<mutex> lock(m);
        return static_cast<Payload *>(::operator new(sizeof(Payload)));
    }
    void deallocate(Payload *p)
    {
        lock_guard<mutex> lock(m);
        ::operator delete(p);
    }
};

// Same churn pattern for every variant: keep a sliding window of ~100 live
// objects, constantly allocating and freeing, so the allocator is genuinely
// exercised on every iteration rather than just growing unbounded.
template <typename Allocator>
void worker(Allocator &alloc, int ops)
{
    vector<Payload *> live;
    live.reserve(101);

    for (int i = 0; i < ops; i++)
    {
        Payload *p = alloc.allocate();
        new (p) Payload{i, i, i, i};
        live.push_back(p);

        if (live.size() > 100)
        {
            Payload *old = live.front();
            live.erase(live.begin());
            old->~Payload();
            alloc.deallocate(old);
        }
    }

    for (auto p : live)
    {
        p->~Payload();
        alloc.deallocate(p);
    }
}

template <typename Allocator>
long long run(int num_threads, int ops_per_thread)
{
    Allocator alloc;

    auto start = steady_clock::now();

    vector<thread> threads;
    for (int t = 0; t < num_threads; t++)
        threads.emplace_back(worker<Allocator>, ref(alloc), ops_per_thread);

    for (auto &t : threads)
        t.join();

    auto end = steady_clock::now();
    return duration_cast<nanoseconds>(end - start).count();
}

int main()
{
    const int total_ops = 2000000;

    for (int num_threads : {1, 2, 4, 8})
    {
        int ops_per_thread = total_ops / num_threads;

        long long pool_ns = run<PoolWrapper>(num_threads, ops_per_thread);
        long long locked_ns = run<LockedPlainWrapper>(num_threads, ops_per_thread);

        cout << "Threads: " << num_threads << "\n"
             << "  Pool allocator    : " << pool_ns / 1000000 << " ms total, "
             << pool_ns / total_ops << " ns/op avg\n"
             << "  Locked new/delete : " << locked_ns / 1000000 << " ms total, "
             << locked_ns / total_ops << " ns/op avg\n\n";
    }

    return 0;
}