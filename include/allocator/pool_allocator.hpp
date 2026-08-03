#pragma once
#include "./memory_pool.hpp"
using namespace std;

template <typename T>

// acts as a bridge that allows containers to use MemoryPool for managing their memory.

class PoolAllocator
{
private:
    static MemoryPool pool;

public:
    using value_type = T;

    PoolAllocator() = default;

    template <typename U>
    PoolAllocator(const PoolAllocator<U> &) {}

    T *allocate(size_t n)
    {
        return static_cast<T *>(
            pool.allocate(n * sizeof(T)) // Calculate total bytes and request from the pool
        );
    }

    void deallocate(T *p, size_t)
    {
        pool.deallocate(p); // Return the memory block back to the pool's free list
    }
};

template <typename T>
MemoryPool PoolAllocator<T>::pool;