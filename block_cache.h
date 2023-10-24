#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "config.h"

#include "cache_policy/cache_policy.h"
#include "cache_policy/lru_policy.h"

#include "db/block_db.h"
#include "db/db.h"

enum class BlockCacheError { Uninitialized, WriteKeyExists };

namespace fs = std::filesystem;

template <typename K, typename V> class BlockCache {
  using DefaultCachePolicy = CachePolicy<K, V>;

public:
  // explicit BlockCache() = default;

  explicit BlockCache(BlockCacheConfig block_cache_config_)
      : block_cache_config(std::move(block_cache_config_)) {
    if (block_cache_config.db_type == "block_db") {
      db = std::make_unique<BlockDB>();
      db->init(block_cache_config);
    } else {
      panic("Block db type '{}' is not supported", block_cache_config.db_type);
    }

    if (block_cache_config.policy_type == "lru") {
      cache = std::make_unique<LRUCache<K, V>>(
          block_cache_config.cache.lru.cache_size);
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

  void put(const K &k, const V &v) {
    if (cache->exist(k)) {

    } else {
      cache->put(k, v);
      if (auto err = db->put(k, v); err != DBError::None) {
        panic("Error writing: {}", magic_enum::enum_name(err));
      }
    }
  }

  V get(const K &k) {
    if (cache->exist(k)) {
      return cache->get(k);
    } else {
      cache_miss++;
      if (auto result_or_err = db->get(k)) {
        return result_or_err.value();
      } else {
        // panic("value for key {} does not exist");
      }
      return V{};
    }
  }

  void dump_cache(fs::path p) {
    std::ofstream ofs(p);
    if (!ofs) {
      panic("Unable to open file {}", p.string());
    }
    cache->dump(ofs);
  }

private:
  BlockCacheConfig block_cache_config;
  std::unique_ptr<DB> db = nullptr;
  std::unique_ptr<DefaultCachePolicy> cache = nullptr;

  uint64_t cache_miss = 0;
};