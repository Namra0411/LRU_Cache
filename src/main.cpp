#include <iostream>
#include <thread>
#include <string>
#include "../include/containers/lru_cache.hpp"
using namespace std;

// Two independent LRU caches are used: one for user sessions and one for cached database/query results.
// Each worker thread simulates real application traffic by inserting, reading, and triggering LRU eviction.
// The threads run concurrently, demonstrating that each cache safely handles its own operations.
void sessionWorker(LRUCache<int, string> &sessionCache)
{
    sessionCache.put(101, "user_101_session");
    sessionCache.put(102, "user_102_session");
    cout << "session: " << sessionCache.get(101) << "\n";
    sessionCache.put(103, "user_103_session"); // evicts session 102
}

void queryWorker(LRUCache<string, int> &queryCache)
{
    queryCache.put("SELECT_orders", 42);
    queryCache.put("SELECT_users", 17);
    cout << "cached result: " << queryCache.get("SELECT_orders") << "\n";
    queryCache.put("SELECT_products", 99); // evicts SELECT_users
}

int main()
{
    LRUCache<int, string> sessionCache(2);   // e.g. userId -> session token
    LRUCache<string, int> queryCache(2);     // e.g. query hash -> result

    thread t1(sessionWorker, ref(sessionCache));
    thread t2(queryWorker, ref(queryCache));

    t1.join();
    t2.join();

    cout << "Done\n";
}