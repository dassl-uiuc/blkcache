#pragma once

#include <cstdint>

#include "utils.h"

template <typename KeyType, typename ValueType> class CachePolicy {
public:
  CachePolicy(uint64_t cache_size_) : cache_size(cache_size_) {}

  virtual void put(const KeyType &key, const ValueType &val, bool owning = false) = 0;
  virtual ValueType get(const KeyType &key) = 0;
  virtual bool exist(const KeyType &key) = 0;
  virtual void remove(const KeyType &key) = 0;
  virtual void dump(std::ostream &os) = 0;

protected:
  std::size_t cache_size;
};