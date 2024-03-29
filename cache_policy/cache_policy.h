#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "config.h"
#include "db/block_db.h"
#include "utils.h"

struct KeyValue
{
  uint64_t* key;
  std::span<uint8_t> value;
};

struct RDMACacheIndex
{
  void* key_value_ptr_offset;
};

struct RDMAKeyValueStorage
{
  struct Data
  {
    uint64_t key;
    // std::array<uint8_t, > value;
  };

  RDMAKeyValueStorage(BlockCacheConfig block_cache_config)
  {
    auto key_value_buffer_size = 1024 * 1024 * 1024;
    // auto cache_num_entries = block_cache_config.cache.thread_safe_lru.cache_size;
    auto storage_num_entries = block_cache_config.db.block_db.num_entries;

    key_value_buffer = std::malloc(key_value_buffer_size);
    cache_index_mbr = std::make_unique<std::pmr::monotonic_buffer_resource>(key_value_buffer, key_value_buffer_size);
    cache_index_pa = std::make_unique<std::pmr::polymorphic_allocator<uint8_t>>(cache_index_mbr.get());

    auto key_size = sizeof(Data);
    auto value_size = 100;

    key_value_size = key_size + value_size;

    // Initialize cache index
    cache_index_buffer = reinterpret_cast<RDMACacheIndex*>(std::malloc(storage_num_entries * sizeof(RDMACacheIndex)));
    std::memset(cache_index_buffer, 0, storage_num_entries * sizeof(RDMACacheIndex));
  }

  KeyValue allocate(uint64_t key_index)
  {
    // key value
    auto ptr = cache_index_pa->allocate(key_value_size);

    // Initialize key
    uint64_t* key = reinterpret_cast<uint64_t*>(ptr);
    *key = key_index;

    // Initialize in cache index
    auto key_value_ptr_offset = (uint8_t*)ptr - (uint8_t*)key_value_buffer;
    info("INSERT {} {}", key_index, key_value_ptr_offset);
    cache_index_buffer[key_index] = RDMACacheIndex{ (void*)key_value_ptr_offset };

    // Initialize value
    std::span<uint8_t> value = std::span<uint8_t>(ptr + sizeof(uint64_t), key_value_size - sizeof(uint64_t));

    auto key_value = KeyValue{ key, value };
    return key_value;
  }

  void deallocate(KeyValue key_value)
  {
    info("DEL {} {}", *key_value.key, 0);
    cache_index_buffer[*key_value.key] = RDMACacheIndex{ 0 };
    *key_value.key = -1;
    cache_index_pa->deallocate((uint8_t*)key_value.key, key_value_size);
  }

  RDMACacheIndex* get_cache_index_buffer()
  {
    return cache_index_buffer;
  }  

private:
  void* key_value_buffer{};
  RDMACacheIndex* cache_index_buffer{};

  std::unique_ptr<std::pmr::monotonic_buffer_resource> cache_index_mbr = nullptr;
  std::unique_ptr<std::pmr::polymorphic_allocator<uint8_t>> cache_index_pa = nullptr;

  size_t key_value_size;
};

template <typename KeyType, typename ValueType> class CachePolicy {
public:
  CachePolicy(BlockCacheConfig block_cache_config_,
              std::shared_ptr<BlockDB> block_db_, uint64_t cache_size_)
      : block_cache_config(block_cache_config_), block_db(block_db_),
        cache_size(cache_size_) {
    block_db_num_entries = block_cache_config.db.block_db.num_entries;
    block_db_block_size = block_cache_config.db.block_db.block_size;
  }

  virtual void put(const KeyType &key, const ValueType &val,
                   bool owning = false) = 0;
  virtual ValueType get(const KeyType &key) = 0;
  virtual bool exist(const KeyType &key) = 0;
  virtual void remove(const KeyType &key) = 0;
  virtual void dump(std::ostream &os) = 0;
  virtual RDMAKeyValueStorage* get_rdma_key_value_storage() { return nullptr; }

protected:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<BlockDB> block_db;
  std::size_t cache_size;

  uint64_t block_db_num_entries;
  uint64_t block_db_block_size;
};