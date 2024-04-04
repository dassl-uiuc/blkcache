#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>
#include <list>
#include <unordered_map>
#include <span>

#include "concurrentqueue.h"
#include "parallel_hashmap/phmap.h"
#include "thread_safe_lru/nchance-cache.h"

typedef tstarling::ThreadSafeStringKey String;
typedef String::HashCompare HashCompare;

using RDMAFriendlyString = tstarling::ThreadSafeStringKey;

template <typename KeyType, typename ValueType>
class ThreadSafeLRUNchanceCache : public CachePolicy<KeyType, ValueType> {
public:
  using Cache = tstarling::ThreadSafeLRUNchanceCache<String, std::string, HashCompare>;
  
  ThreadSafeLRUNchanceCache(BlockCacheConfig block_cache_config_,
                     std::shared_ptr<BlockDB> block_db, uint64_t cache_size)
      : block_cache_config(block_cache_config_), CachePolicy<KeyType, ValueType>(block_cache_config, block_db,
                                        cache_size) {
    if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    {
      rdma_key_value_storage = std::make_shared<RDMAKeyValueStorage>(block_cache_config);
    }
    secm = std::make_shared<Cache>(cache_size, block_cache_config, rdma_key_value_storage);
  }

  void put(const KeyType &key, const ValueType &val,
           bool owning = false) override {
    String skey(key.c_str(), key.length());
    secm->insert(skey, val);
    for (const auto& callback : this->write_callbacks) {
      callback(key, val);
    }
  }

  ValueType get(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    Cache::ConstAccessor ac;
    ValueType ret;
    if (secm->find(ac, skey)) {
      ret = *ac;
    }
    for (const auto& callback : this->read_callbacks) {
      callback(key);
    }

    return ret;
  }

  bool exist(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    Cache::ConstAccessor ac;
    if (secm->find(ac, skey)) {
      return true;
    }
    return false;
  }

  void remove(const KeyType &key) override { panic("Unsupported"); }

  void dump(std::ostream &os) override {
    std::vector<String> skeys;
    secm->snapshotKeys(skeys);
    for (auto &skey : skeys) {
      os << skey.data() << "\n";
    }
  }

  void add_callback_on_eviction(EvictionCallback<KeyType, ValueType> callback) {
    this->eviction_callbacks.emplace_back(callback);
    secm->add_callback_on_eviction(callback);
  }

  RDMAKeyValueStorage* get_rdma_key_value_storage() override { return rdma_key_value_storage.get(); }

private:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<Cache> secm = nullptr;
  std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage = nullptr;
};