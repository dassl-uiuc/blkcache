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
    info("Access rate: {} and access per itr: {}", access_rate, access_per_itr);
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
    update_frequency(key);
    if(get_frequency(key) >= access_rate){
      // info("Access rate match for key: {}", key);
      put(key, val, owning);
      return true;
    }
    return false;
  }

  ValueType get(const KeyType &key) override {
    String skey(key.c_str(), key.length());
    uint64_t current_accesses = total_accesses.fetch_add(1, std::memory_order_relaxed) + 1;
    if(current_accesses > access_per_itr){
        if (total_accesses.load(std::memory_order_relaxed) >= access_per_itr) {
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

  void add_callback_on_eviction(EvictionCallback<KeyType, ValueType> callback) override {
    this->eviction_callbacks.emplace_back(callback);
    secm->add_callback_on_eviction(callback);
  }

  RDMAKeyValueStorage* get_rdma_key_value_storage() override { return rdma_key_value_storage.get(); }

  using ConstFrequencyAccessor = tbb::concurrent_hash_map<KeyType, uint64_t>::const_accessor;
  using FrequencyAccessor = tbb::concurrent_hash_map<KeyType, uint64_t>::accessor;

  uint64_t get_frequency(const KeyType& key) {
    ConstFrequencyAccessor acc;
    if (key_freq.find(acc, key)) {
      return acc->second;
    }
    return 0;
}

  void update_frequency(const KeyType& key) {
    wait_on_isclearing();
    bool found = false;
    {
      FrequencyAccessor acc;
      if (key_freq.find(acc, key)) {
        acc->second++;
        found = true;
      }
    }
    if (!found)
    {
      FrequencyAccessor acc;
      key_freq.insert(acc, key);
      acc->second = 1;
    }
}

  void clear_frequency() {
    is_clearing.store(true);
    std::vector<KeyType> keys;

    // Iterate over the map to collect keys
    for (auto it = key_freq.begin(); it != key_freq.end(); ++it) {
        keys.push_back(it->first);
    }

    // Remove each key collected
    for (auto& key : keys) {
        key_freq.erase(key);
    }
    is_clearing.store(false);
  }

  void wait_on_isclearing() {
    while (is_clearing.load()) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }


private:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<Cache> secm = nullptr;
  std::shared_ptr<RDMAKeyValueStorage> rdma_key_value_storage = nullptr;
  std::atomic<uint64_t> total_accesses;
  std::atomic<bool> is_clearing;
  uint64_t access_rate;
  uint64_t access_per_itr;

  tbb::concurrent_hash_map<KeyType, uint64_t> key_freq;
  std::mutex key_freq_mutex;
};