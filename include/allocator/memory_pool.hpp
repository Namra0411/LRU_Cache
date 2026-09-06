#pragma once
#include <cstddef>
#include <atomic>
#include <mutex>
#include <new>
using namespace std;

/*
    MemoryPool:
    - Manages fixed size blocks in slabs.
    - Uses a lock free free list for fast allocate/deallocate.
    - Uses a mutex only when we need to get a new slab.
*/
class MemoryPool
{
    struct alignas(64) Block
    {
        Block *next;
    };

    struct Slab
    {
        void *memory;
        Slab *next;
    };

    static const size_t SLAB_BLOCKS = 64; // blocks pulled in per refill

    atomic<Block *> freeList;
    atomic<size_t> blockSize; // fixed once, on first allocate() call
    mutex refillMtx;          // only taken when the free list runs out, or on first init
    Slab *slabList;           // only touched under mutex , so it's plain

    // Grab a new slab of SLAB_BLOCKS blocks and push them all onto the
    // lock-free free list. Called with refillMtx held.
    void refill()
    {
        size_t bytes = blockSize.load(memory_order_relaxed) * SLAB_BLOCKS;
        char *base = static_cast<char *>(::operator new(bytes, std::align_val_t(64)));
        Slab *slab = new Slab;
        slab->memory = base;
        slab->next = slabList;
        slabList = slab;

        size_t bs = blockSize.load(memory_order_relaxed);
        for (size_t i = 0; i < SLAB_BLOCKS; i++)
        {
            Block *b = reinterpret_cast<Block *>(base + i * bs);
            push(b);
        }
    }

    // Lock-free push onto the free list (Treiber stack).
    void push(Block *b)
    {
        Block *oldHead = freeList.load(memory_order_relaxed);
        do
        {
            b->next = oldHead;
        } while (!freeList.compare_exchange_weak(
            oldHead, b, memory_order_release, memory_order_relaxed));
    }

    // Lock-free pop from the free list. Returns nullptr if empty.
    Block *pop()
    {
        Block *oldHead = freeList.load(memory_order_acquire);
        while (oldHead &&
               !freeList.compare_exchange_weak(
                   oldHead, oldHead->next, memory_order_acquire, memory_order_relaxed))
        {
            // oldHead is refreshed by compare_exchange_weak on failure; retry.
        }
        return oldHead;
    }

public:
    MemoryPool() : freeList(nullptr), blockSize(0), slabList(nullptr) {}

    ~MemoryPool()
    {
        Slab *s = slabList;
        while (s)
        {
            Slab *next = s->next;
            ::operator delete(s->memory, std::align_val_t(64));
            delete s;
            s = next;
        }
    }

    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    void *allocate(size_t size)
    {
        //doubke check lock for initialization
        if (blockSize.load(memory_order_acquire) == 0)
        {
            lock_guard<mutex> lock(refillMtx);
            if (blockSize.load(memory_order_relaxed) == 0)
            {
                size_t bs = size > sizeof(Block) ? size : sizeof(Block);
                bs = (bs + 63) & ~size_t(63);
                blockSize.store(bs, memory_order_release);
            }
        }

        Block *block = pop();
        if (!block)
        {
            lock_guard<mutex> lock(refillMtx);
            block = pop(); // someone else may have refilled while we waited
            if (!block)
            {
                refill();
                block = pop();
            }
        }
        return block;
    }

    void deallocate(void *ptr)
    {
        push(static_cast<Block *>(ptr));
    }
};