#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>
#include <list>
#include <unordered_map>

template <typename KeyType, typename ValueType>
class LRUCache : public CachePolicy<KeyType, ValueType> {
public:
  LRUCache(BlockCacheConfig block_cache_config, std::shared_ptr<BlockDB> block_db, uint64_t cache_size) :
    CachePolicy<KeyType, ValueType>(block_cache_config, block_db, cache_size)
  {
  }

  void put(const KeyType &key, const ValueType &val, bool owning = false) override {
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
  // paged
  // std::vector<

  std::list<std::pair<KeyType, ValueType>> item_list;
  std::unordered_map<KeyType, decltype(item_list.begin())> item_map;
};
