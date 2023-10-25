#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>
#include <list>
#include <random>
#include <unordered_map>

template <typename KeyType, typename ValueType>
class RandomCache : public CachePolicy<KeyType, ValueType> {
public:
  RandomCache(uint64_t cache_size)
      : CachePolicy<KeyType, ValueType>(cache_size) {
    distribution = std::uniform_int_distribution<int>(0, cache_size - 1);
  }

  void put(const KeyType &key, const ValueType &val) override {
    info("put {} {}", key, val);
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

  void dump(std::ofstream &os) override {
    for (const auto &[k, v] : item_map) {
      os << k << "\n";
    }
  }

private:
  void clean(void) {
    while (item_map.size() > this->cache_size) {
      auto index = distribution(generator);
      // Linearly scan to the index, might be slow
      auto it = std::next(std::begin(item_list), index);
      item_map.erase(it->first);
      item_list.erase(it);
    }
  };

private:
  std::default_random_engine generator{std::random_device{}()};
  std::uniform_int_distribution<int> distribution;
  std::list<std::pair<KeyType, ValueType>> item_list;
  std::unordered_map<KeyType, decltype(item_list.begin())> item_map;
};
