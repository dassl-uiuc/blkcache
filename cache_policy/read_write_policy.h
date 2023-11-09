#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>

template <typename KeyType, typename ValueType>
class ReadWriteCache : public CachePolicy<KeyType, ValueType> {
public:
  ReadWriteCache(uint64_t cache_size, std::unique_ptr<CachePolicy<KeyType, ValueType>> read_cache_, std::unique_ptr<CachePolicy<KeyType, ValueType>> write_cache_)
      : CachePolicy<KeyType, ValueType>(cache_size), read_cache(std::move(read_cache_)), write_cache(std::move(write_cache_)) {
  }

  void put(const KeyType &key, const ValueType &val, bool is_write = false) override {
    if (is_write) {
      write_cache->put(key, val, true);
    } else {
      read_cache->put(key, val, true);
    }
  }

  ValueType get(const KeyType &key) override {
    if (read_cache->exist(key)) {
      return read_cache->get(key);
    } else if (write_cache->exist(key)) {
      return write_cache->get(key);
    } else {
      panic("Key {} does not exist", key);
    }
    return ValueType{};
  }

  bool exist(const KeyType &key) override { 
    return read_cache->exist(key) || write_cache->exist(key);
  }

  void remove(const KeyType &key) override { panic("Unsupported"); }

  void dump(std::ofstream &os) override {
    read_cache->dump(os);
    write_cache->dump(os);
  }

private:
  std::unique_ptr<CachePolicy<KeyType, ValueType>> read_cache;
  std::unique_ptr<CachePolicy<KeyType, ValueType>> write_cache;
};
