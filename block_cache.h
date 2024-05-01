#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "config.h"

#include "cache_policy/cache_policy.h"
#include "cache_policy/lru_policy.h"
#include "cache_policy/random_policy.h"
#include "cache_policy/split_policy.h"
#include "cache_policy/thread_safe_lru_policy.h"
#include "cache_policy/nchance_policy.h"
#include "cache_policy/access_rate_policy.h"
#include "cache_policy/access_rate_policy_dynamic.h"

#include "db/block_db.h"
#include "db/db.h"

enum class BlockCacheError { Uninitialized, WriteKeyExists };

namespace fs = std::filesystem;


template<class T>
class CopyableAtomic : public std::atomic<T>
{
public:
    //defaultinitializes value
    CopyableAtomic() = default;

    constexpr CopyableAtomic(T desired) : 
        std::atomic<T>(desired) 
    {}

    constexpr CopyableAtomic(const CopyableAtomic<T>& other) :
        CopyableAtomic(other.load(std::memory_order_relaxed))
    {}

    // operator T&()
    // {
    //   return *this;
    // }

    // operator T() const
    // {
    //   return this->load(std::memory_order_relaxed);
    // }

    CopyableAtomic& operator=(const CopyableAtomic<T>& other) {
        this->store(other.load(std::memory_order_acquire), std::memory_order_release);
        return *this;
    }
};

template <typename K, typename V> class BlockCache {
  using DefaultCachePolicy = CachePolicy<K, V>;

public:
  explicit BlockCache() = default;

  explicit BlockCache(BlockCacheConfig block_cache_config_)
      : block_cache_config(std::move(block_cache_config_)) {
    if (block_cache_config.db_type == "block_db") {
      db = std::make_shared<BlockDB>();
      db->init(block_cache_config);
    } else {
      panic("Block db type '{}' is not supported", block_cache_config.db_type);
    }

    auto make_lru_cache = [&](const auto &cache_size) {
      return std::make_shared<LRUCache<K, V>>(block_cache_config, db,
                                              cache_size);
    };

    auto make_random_cache = [&](const auto &cache_size) {
      return std::make_shared<RandomCache<K, V>>(block_cache_config, db,
                                                 cache_size);
    };

    if (block_cache_config.policy_type == "lru") {
      cache = make_lru_cache(block_cache_config.cache.lru.cache_size);
    } else if (block_cache_config.policy_type == "random") {
      cache = make_random_cache(block_cache_config.cache.random.cache_size);
    } else if (block_cache_config.policy_type == "split") {
      if (block_cache_config.cache.split.owning_ratio +
              block_cache_config.cache.split.nonowning_ratio !=
          1.0) {
        panic("Read ratio and write ratio must sum to 1.0");
      }

      std::shared_ptr<DefaultCachePolicy> owning_cache = nullptr;
      std::shared_ptr<DefaultCachePolicy> nonowning_cache = nullptr;

      auto owning_cache_size =
          static_cast<uint64_t>(block_cache_config.cache.split.cache_size *
                                block_cache_config.cache.split.owning_ratio);
      auto nonowning_cache_size =
          static_cast<uint64_t>(block_cache_config.cache.split.cache_size *
                                block_cache_config.cache.split.nonowning_ratio);

      auto make_cache = [&](std::string_view cache_type, auto cache_size)
      {
        std::shared_ptr<DefaultCachePolicy> new_cache;
        if (cache_type == "lru") {
          new_cache = make_lru_cache(cache_size);
        } else if (cache_type == "random") {
          new_cache = make_random_cache(cache_size);
        } else if (cache_type == "thread_safe_lru") {
          new_cache = std::make_shared<ThreadSafeLRUCache<K, V>>(
              block_cache_config, db, cache_size);
        } else if (cache_type == "nchance") {
          new_cache = std::make_shared<ThreadSafeLRUNchanceCache<K, V>>(
              block_cache_config, db, cache_size);
        } else if (cache_type == "access_rate") {
          info("Creating access rate cache with access rate: {}, access per itr: {}", block_cache_config.access_rate, block_cache_config.access_per_itr);
          new_cache = std::make_shared<ThreadSafeLRUAccessRateCache<K, V>>(
              block_cache_config, db,
              cache_size, 
              block_cache_config.access_rate,
              block_cache_config.access_per_itr);
        } else if (cache_type == "access_rate_dynamic") {
          info("Creating access rate dynamic cache");
          new_cache = std::make_shared<ThreadSafeLRUAccessRateDynamicCache<K, V>>(
              block_cache_config, db,
              cache_size, 
              block_cache_config.access_rate,
              block_cache_config.access_per_itr);
        } else {
          panic("Read cache type '{}' is not supported",
                cache_type);
        }

        return new_cache;        
      };

      owning_cache = make_cache(block_cache_config.cache.split.owning_cache_type, owning_cache_size);
      nonowning_cache = make_cache(block_cache_config.cache.split.nonowning_cache_type, nonowning_cache_size);

      cache = std::make_shared<SplitCache<K, V>>(
          block_cache_config, db, block_cache_config.cache.split.cache_size,
          std::move(owning_cache), std::move(nonowning_cache));
    } else if (block_cache_config.policy_type == "thread_safe_lru") {
      cache = std::make_shared<ThreadSafeLRUCache<K, V>>(
          block_cache_config, db,
          block_cache_config.cache.thread_safe_lru.cache_size);
    } else if (block_cache_config.policy_type == "nchance") {
      cache = std::make_shared<ThreadSafeLRUNchanceCache<K, V>>(
          block_cache_config, db,
          block_cache_config.cache.thread_safe_lru.cache_size);
    } else if (block_cache_config.policy_type == "access_rate") {
      info("Creating access rate cache with access rate: {}, access per itr: {}", block_cache_config.access_rate, block_cache_config.access_per_itr);
      cache = std::make_shared<ThreadSafeLRUAccessRateCache<K, V>>(
          block_cache_config, db,
          block_cache_config.cache.thread_safe_lru.cache_size, 
          block_cache_config.access_rate,
          block_cache_config.access_per_itr);
    } else if (block_cache_config.policy_type == "access_rate_dynamic") {
      info("Creating access rate dynamic cache");
      cache = std::make_shared<ThreadSafeLRUAccessRateDynamicCache<K, V>>(
          block_cache_config, db,
          block_cache_config.cache.thread_safe_lru.cache_size, 
          block_cache_config.access_rate,
          block_cache_config.access_per_itr);
    } else {
      panic("Block policy type '{}' is not supported",
            block_cache_config.policy_type);
    }
  }

  static BlockCache<K, V> InitializeFromConfigFile(fs::path path) {
    std::ifstream ifs(path);
    if (!ifs) {
      panic("Initializing block cache from '{}' does not exist", path.string());
    }
    json j = json::parse(ifs);
    auto block_cache_config = j.template get<BlockCacheConfig>();
    return BlockCache<K, V>(block_cache_config);
  }

  const BlockCacheConfig &get_config() const { return block_cache_config; }

  void put(const K &k, const V &v, bool owning = true) {
    writes += 1;
    // if (cache->exist(k)) {
    // }
    if (auto err = db->put(k, v); err != DBError::None) {
      panic("Error writing: {}", magic_enum::enum_name(err));
    }
    cache->put(k, v, owning);
  }

  bool exists_in_cache(const K &k) { return cache->exist(k); }

  V get(const K &k, bool owning = true, bool exists_in_cache = false) {
    reads += 1;
    if (exists_in_cache || cache->exist(k)) {
      cache_hit++;
      return cache->get(k);
    } else {
      cache_miss++;
      if (auto result_or_err = db->get(k)) {
        cache_not_compulsory_miss++;
        V v = result_or_err.value();

        // Put the result in the cache
        cache->put(k, v, owning);

        return v;
      } else {
        cache_compulsory_miss++;

        // Put dummy value in the cache
        cache->put(k, V{}, owning);

        panic("value for key {} does not exist", k);
      }
      return V{};
    }
  }

  RDMAKeyValueStorage* get_rdma_key_value_storage() {
    return cache->get_rdma_key_value_storage();
  }
  
  void increment_cache_hit() { cache_hit++; }
  void increment_cache_miss() { cache_miss++; }
  void increment_cache_compulsory_miss() { cache_compulsory_miss++; }
  void increment_cache_not_compulsory_miss() { cache_not_compulsory_miss++; }

  auto get_cache() { return cache; }

  auto get_db() { return db; }

  void dump_cache(fs::path p) {
    std::ofstream ofs(p, std::ios::out | std::ios::trunc);
    if (!ofs) {
      panic("Unable to open file {}", p.string());
    }
    cache->dump(ofs);
  }

  json dump_cache_info_as_json()
  {
    json j;
    j["writes"] = writes.load(std::memory_order_relaxed);
    j["reads"] = reads.load(std::memory_order_relaxed);
    j["cache_hit"] = cache_hit.load(std::memory_order_relaxed);
    j["cache_miss"] = cache_miss.load(std::memory_order_relaxed);
    j["cache_not_compulsory_miss"] = cache_not_compulsory_miss.load(std::memory_order_relaxed);
    j["cache_compulsory_miss"] = cache_compulsory_miss.load(std::memory_order_relaxed);
    j["cache_freq_addition"] = cache_freq_addition.load(std::memory_order_relaxed);
    return j;
  }

  void dump_cache_info(fs::path p) {
    std::ofstream ofs(p, std::ios::out | std::ios::trunc);
    if (!ofs) {
      panic("Unable to open file {}", p.string());
    }
    ofs << dump_cache_info_as_json().dump(2);
  }

  void reset_cache_info()
  {
    writes = 0;
    reads = 0;
    cache_hit = 0;
    cache_miss = 0;
    cache_not_compulsory_miss = 0;
    cache_compulsory_miss = 0;
    cache_freq_addition = 0;
  }

private:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<BlockDB> db = nullptr;
  std::shared_ptr<DefaultCachePolicy> cache = nullptr;
  CopyableAtomic<uint64_t> writes = 0;
  CopyableAtomic<uint64_t> reads = 0;
  CopyableAtomic<uint64_t> cache_hit = 0;
  CopyableAtomic<uint64_t> cache_miss = 0;
  CopyableAtomic<uint64_t> cache_not_compulsory_miss = 0;
  CopyableAtomic<uint64_t> cache_compulsory_miss = 0;
public:
  CopyableAtomic<uint64_t> cache_freq_addition = 0;
};