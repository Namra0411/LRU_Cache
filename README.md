# LRU Cache with Lock-Free Memory Pool

A thread-safe, low-latency LRU (Least Recently Used) cache implemented in modern C++17, backed by a custom lock-free memory pool instead of the default heap allocator.

## Project Overview

Standard LRU cache implementations rely on `new`/`delete` for every insertion and eviction. Under concurrent access, this puts pressure on the system allocator's internal locking. This project replaces that with a custom `MemoryPool` that reuses fixed-size memory blocks via a lock-free free list, avoiding allocator lock contention entirely.

The cache itself exposes O(1) `get`/`put` and is designed to be safe to call from multiple threads.

## Key Design Decisions

- **Hash map + intrusive doubly linked list** for O(1) lookup and O(1) reordering on access.
- **Custom memory pool** for node allocation, instead of per-node `new`/`delete`.
- **Lock-free free list** (Treiber stack, `std::atomic` compare-exchange) for the allocator's hot path — no mutex on the common allocate/deallocate case.
- **Slab preallocation** — blocks are pulled in 64 at a time from the OS, not one at a time, reducing the number of system allocations.
- **Cache-line alignment** (`alignas(64)`) on pool blocks, to avoid false sharing between blocks used by different threads.
- **Single mutex guarding the cache's map/list state** (`get`/`put`), since the shared hash map and linked list pointers are not otherwise safe under concurrent mutation.

## File Structure
include/
├── allocator/
│ ├── memory_pool.hpp # Lock-free, slab-based memory pool
│ └── pool_allocator.hpp # Adapter: bridges std-style allocator interface to MemoryPool
├── containers/
│ └── lru_cache.hpp # LRUCache: hash map + intrusive DLL, mutex-guarded
src/
└── main.cpp # Basic usage demo

## Data Structures Used

| Structure | Purpose |
|---|---|
| `unordered_map<K, Node*>` | O(1) key lookup |
| Intrusive doubly linked list | O(1) move-to-front on access, O(1) eviction from the tail |
| Intrusive singly linked free list (`Block*`, atomic) | O(1) lock-free allocate/deallocate in `MemoryPool` |
| Singly linked list of slabs | Tracks raw memory chunks so they can be released in the pool's destructor |

## Time Complexity

| Operation | Time | Notes |
|---|---|---|
| `get(key)` | O(1) | Map lookup + list unlink/relink |
| `put(key, value)` | O(1) | Map insert/update + list unlink/relink + possible eviction |
| `MemoryPool::allocate` | O(1) amortized | O(1) on cache hit against free list; occasional O(SLAB_BLOCKS) refill, amortized to O(1) per allocation |
| `MemoryPool::deallocate` | O(1) | Single CAS push onto the free list |
| Space | O(N) | N = cache capacity, plus pool overhead in multiples of the slab size |

## What Makes This Different From a Textbook LRU Cache

1. **Custom allocator, not just custom data structure.** Most LRU cache implementations stop at the hash-map-plus-linked-list design. This one also replaces the allocation strategy underneath it.
2. **Lock-free, not just "has a mutex."** The memory pool's hot path uses atomic CAS instead of a mutex — a meaningfully different (and harder to get right) concurrency approach than simply wrapping everything in a lock.

## Benchmarks Results

### LRU Cache — single-threaded allocator overhead
Pool vs system allocator vs a mutex-guarded allocator, no threads/contention
involved — isolates the raw cost of each allocation strategy.

<img width="1387" height="391" alt="Screenshot 2026-09-06 180423" src="https://github.com/user-attachments/assets/fe90d081-84e3-4769-91f7-bfe83a413cd9" />

### Memory Pool — multi-threaded, isolated from the cache's lock
Pool vs a mutex-guarded allocator only, with LRUCache's own mutex removed
from the picture — the actual scenario the lock-free design targets.

<img width="1373" height="746" alt="Screenshot 2026-09-06 180407" src="https://github.com/user-attachments/assets/4080a882-4294-47da-81ad-bdee1f57b7d4" />

## Building and Running

```bash
# Basic demo
g++ -std=c++17 src/main.cpp -I ./include -o lru_cache
./lru_cache
```
