#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>

template <typename KeyType, typename ValueType>
class SplitCache : public CachePolicy<KeyType, ValueType> {
public:
  SplitCache(uint64_t cache_size, std::unique_ptr<CachePolicy<KeyType, ValueType>> owning_cache_, std::unique_ptr<CachePolicy<KeyType, ValueType>> nonowning_cache_)
      : CachePolicy<KeyType, ValueType>(cache_size), owning_cache(std::move(owning_cache_)), nonowning_cache(std::move(nonowning_cache_)) {
  }

  void put(const KeyType &key, const ValueType &val, bool owning = true) override {
    if (owning) {
      owning_cache->put(key, val, true);
    } else {
      nonowning_cache->put(key, val, true);
    }
  }

  ValueType get(const KeyType &key) override {
    if (owning_cache->exist(key)) {
      return owning_cache->get(key);
    } else if (nonowning_cache->exist(key)) {
      return nonowning_cache->get(key);
    } else {
      panic("Key {} does not exist", key);
    }
    return ValueType{};
  }

  bool exist(const KeyType &key) override { 
    return owning_cache->exist(key) || nonowning_cache->exist(key);
  }

  void remove(const KeyType &key) override { panic("Unsupported"); }

  void dump(std::ostream &os) override {
    owning_cache->dump(os);
    nonowning_cache->dump(os);
  }

private:
  std::unique_ptr<CachePolicy<KeyType, ValueType>> owning_cache;
  std::unique_ptr<CachePolicy<KeyType, ValueType>> nonowning_cache;
};
