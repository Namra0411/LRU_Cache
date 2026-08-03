#include <iostream>
#include "../include/containers/lru_cache.hpp"
using namespace std;
int main()
{
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);

    cout << cache.get(1) << "\n"; // 10

    cache.put(3, 30); // evicts key 2

    try
    {
        cout << cache.get(2) << "\n";
    }
    catch (...)
    {
        cout << "Key 2 evicted\n";
    }
}