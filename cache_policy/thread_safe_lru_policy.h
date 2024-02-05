#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>
#include <list>
#include <unordered_map>

#include "parallel_hashmap/phmap.h"
#include "concurrentqueue.h"

constexpr size_t ParallelFlatHashMapSubmapSize = 8; //2**N submaps

template<typename T, typename T2>
using ThreadSafeMap = phmap::parallel_flat_hash_map<T, T2,
                                phmap::priv::hash_default_hash<T>,
                                phmap::priv::hash_default_eq<T>,
                                phmap::priv::Allocator<phmap::priv::Pair<const T, T2>>,
                                ParallelFlatHashMapSubmapSize,
                                std::mutex>;
#define NOT_IN_LIST 1

template <typename K, typename V>
struct ListNode {
  ListNode()
    : prev(NOT_IN_LIST), next(nullptr)
  {}

  ListNode(const K& key, const V& val)
    : prev(NOT_IN_LIST), next(nullptr), k(key), v(val)
  {}

  bool IsInList() const {
    return prev != NOT_IN_LIST;
  }

  K k;
  V v;
  ListNode* prev;
  ListNode* next;
};


template <typename KeyType, typename ValueType>
class ThreadSafeLRUCache : public CachePolicy<KeyType, ValueType> {
public:
  ThreadSafeLRUCache(BlockCacheConfig block_cache_config, std::shared_ptr<BlockDB> block_db, uint64_t cache_size) :
    CachePolicy<KeyType, ValueType>(block_cache_config, block_db, cache_size)
  {
  }

  void put(const KeyType &key, const ValueType &val, bool owning = false) override {


    // item_map.modify_if(key,
    //   // key is present
    //   [&](auto& v)
    //   { 
    //     item_list.erase(v);
    //     v.second = val;
    //   },
    //   // key is not present
    //   [&](const auto& ctor) { ctor = std::make_pair(key, val); }
    // );

    auto it = item_map.find(key);
    if (it != item_map.end()) {
      item_list.erase(it->second);
      item_map.erase(it);
    }
    item_list.push_front(make_pair(key, val));
    item_map.insert(make_pair(key, item_list.begin()));
    clean();
  }

  ValueType get(const KeyType &key) override {
    assert(exist(key));
    auto it = item_map.find(key);
    item_list.splice(item_list.begin(), item_list, it->second);
    return it->second->second;
  }

  bool exist(const KeyType &key) override { return (item_map.count(key) > 0); }

  void remove(const KeyType &key) override { panic("Unsupported"); }

  void dump(std::ostream &os) override {
    for (const auto &[k, v] : item_list) {
      os << k << "\n";
    }
  }

private:
  void clean(void) {
    while (item_map.size() > this->cache_size) {
      auto last_it = item_list.end();
      last_it--;
      item_map.erase(last_it->first);
      item_list.pop_back();
    }
  };

private:
  std::list<std::pair<KeyType, ValueType>> item_list;
  std::unordered_map<KeyType, decltype(item_list.begin())> item_map;
  // ThreadSafeMap<KeyType, ListNode*> item_map;
  // moodycamel::ConcurrentQueue<std::pair<KeyType, ValueType>> item_list;
  ListNode<std::string, std::string>* head;
  ListNode<std::string, std::string>* tail;
  std::mutex m;
};
