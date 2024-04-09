#pragma once

#include "cache_policy/cache_policy.h"

#include <cassert>
#include <list>
#include <unordered_map>
#include <span>
#include <atomic>

#include "concurrentqueue.h"
#include "parallel_hashmap/phmap.h"
#include "thread_safe_lru/access_rate-cache.h"
#include <tbb/concurrent_hash_map.h>

typedef tstarling::ThreadSafeStringKey String;
typedef String::HashCompare HashCompare;

using RDMAFriendlyString = tstarling::ThreadSafeStringKey;

template <typename KeyType, typename ValueType>
class ThreadSafeLRUAccessRateCache : public CachePolicy<KeyType, ValueType> {
public:
  using Cache = tstarling::ThreadSafeLRUAccessRateCache<String, std::string, HashCompare>;
  
  ThreadSafeLRUAccessRateCache(BlockCacheConfig block_cache_config_,
                     std::shared_ptr<BlockDB> block_db, uint64_t cache_size, uint64_t access_rate_ = 1, uint64_t access_per_itr_ = 1000)
      : block_cache_config(block_cache_config_), CachePolicy<KeyType, ValueType>(block_cache_config, block_db,
                                        cache_size) {
    if (block_cache_config.baseline.one_sided_rdma_enabled && block_cache_config.baseline.use_cache_indexing)
    {
      rdma_key_value_storage = std::make_shared<RDMAKeyValueStorage>(block_cache_config);
    }
    secm = std::make_shared<Cache>(cache_size, block_cache_config, rdma_key_value_storage);
    access_rate = access_rate_;
    access_per_itr = access_per_itr_;
    total_accesses = 0;
  }

  void put(const KeyType &key, const ValueType &val,
           bool owning = false) override {
    String skey(key.c_str(), key.length());
    secm->insert(skey, val);
    for (const auto& callback : this->write_callbacks) {
      callback(key, val);
    }
  }

  bool put_access_rate_match(const KeyType &key, const ValueType &val,
           bool owning = false) override {
    String skey(key.c_str(), key.length());
    update_frequency(key);
    if(key_freq[key] >= access_rate){
      secm->insert(skey, val);
      return true;
    }
    return false;
  }

  ValueType get(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    uint64_t current_accesses = total_accesses.fetch_add(1) + 1;
    if(current_accesses > 100000){
      std::lock_guard<std::mutex> lock(key_freq_mutex);
        if (total_accesses.load() >= 100000) {
          info("Clearing frequency");
          clear_frequency();
          total_accesses.store(0);
        }
    }
    
    total_accesses++;
    update_frequency(key);
    
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

  uint64_t get_frequency(const KeyType& key) {
    std::lock_guard<std::mutex> lock(key_freq_mutex);
    return key_freq[key];
  }

  void update_frequency(const KeyType& key) {
    std::lock_guard<std::mutex> lock(key_freq_mutex);
    if (key_freq.find(key) == key_freq.end()) {
        key_freq.emplace(key, 1);
    } else {
        key_freq[key]++;
    }
  }

  void clear_frequency() {
    // std::lock_guard<std::mutex> lock(key_freq_mutex);
    key_freq.clear();
  }


private:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<Cache> secm = nullptr;
  std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage = nullptr;
  std::atomic<uint64_t> total_accesses;
  uint64_t access_rate;
  uint64_t access_per_itr;

  phmap::flat_hash_map<KeyType, uint64_t, std::hash<KeyType>> key_freq;
  std::mutex key_freq_mutex;
};