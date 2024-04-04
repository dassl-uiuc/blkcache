#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "config.h"
#include "db/block_db.h"
#include "utils.h"

// RDMA related, wrong to be put here but oh well

struct KeyValue
{
  uint64_t* key;
  std::span<uint8_t> value;
};

struct RDMACacheIndex
{
  uintptr_t key_value_ptr_offset;
};

struct RDMAKeyValueStorage
{
  struct Data
  {
    uint64_t key;
    // std::array<uint8_t, > value;
  };

  RDMAKeyValueStorage(BlockCacheConfig block_cache_config_) :
    block_cache_config(block_cache_config_)
  {
    key_value_buffer_size = 1024 * 1024 * 1024;

    key_value_buffer = std::malloc(key_value_buffer_size);
    std::memset(key_value_buffer, -1, key_value_buffer_size);
    key_value_mbr = std::make_unique<std::pmr::monotonic_buffer_resource>(key_value_buffer, key_value_buffer_size);
    key_value_upr = std::make_unique<std::pmr::unsynchronized_pool_resource>(key_value_mbr.get());
    key_value_pa = std::make_unique<std::pmr::polymorphic_allocator<uint8_t>>(key_value_upr.get());

    // Initialize cache index
    cache_index_buffer = allocate_cache_index();
  }

  auto get_allocated_cache_index_size()
  {
    auto storage_num_entries = block_cache_config.db.block_db.num_entries; 
    return storage_num_entries * sizeof(RDMACacheIndex);
  }

  RDMACacheIndex* allocate_cache_index()
  {
    auto size = get_allocated_cache_index_size();
    auto buffer = reinterpret_cast<RDMACacheIndex*>(std::malloc(size));
    std::memset(buffer, -1, size);
    return buffer;
  }

  KeyValue allocate(uint64_t key_index)
  {
    // key value
    auto ptr = key_value_pa->allocate(get_key_value_size());

    // Initialize key
    uint64_t* key = reinterpret_cast<uint64_t*>(ptr);
    *key = key_index;

    // Initialize in cache index
    auto key_value_ptr_offset = (uint8_t*)ptr - (uint8_t*)key_value_buffer;
    cache_index_buffer[key_index] = RDMACacheIndex{ (uintptr_t)key_value_ptr_offset };
    // info("WRITE BUFFER {} {} {} {}", (void*)cache_index_buffer, key_index, (void*)&cache_index_buffer[key_index], cache_index_buffer[key_index].key_value_ptr_offset);

    // Initialize value
    std::span<uint8_t> value = std::span<uint8_t>(ptr + sizeof(uint64_t), get_key_value_size() - sizeof(uint64_t));

    auto key_value = KeyValue{ key, value };
    return key_value;
  }

  void deallocate(KeyValue key_value)
  {
    cache_index_buffer[*key_value.key] = RDMACacheIndex{ 0 };
    // info("DEALLOC {} {}", (void*)key_value.key, (uint64_t)*key_value.key);
    *key_value.key = -1;
    key_value_pa->deallocate((uint8_t*)key_value.key, get_key_value_size());
  }

  RDMACacheIndex* get_cache_index_buffer()
  {
    return cache_index_buffer;
  }

  void set_cache_index(int i, RDMACacheIndex* cache_index)
  {
    if (cache_index_buffers.size() < i + 1)
    {
      cache_index_buffers.resize(i + 1);
    }
    cache_index_buffers[i] = cache_index;
  }

  uint64_t get_num_cache_index_buffers_containing_key(uint64_t key_index)
  {
    auto count = 0;
    for (auto i = 0; i < cache_index_buffers.size(); i++)
    {
      if (cache_index_buffers[i][key_index].key_value_ptr_offset != 0)
      {
        count++;
      }
    }
    return count;
  }

  RDMACacheIndex* get_cache_index_buffer_for(int i)
  {
    return cache_index_buffers[i];
  }

  std::size_t get_cache_index_size()
  {
    return cache_index_buffers.size();
  }

  void set_my_cache_index(int i)
  {
    my_cache_index = i;
  }

  int get_my_cache_index()
  {
    return my_cache_index;
  }

  void* get_key_value_buffer() { return key_value_buffer; }
  std::size_t get_key_value_buffer_size() { return key_value_buffer_size; }

  std::size_t get_key_size() { return sizeof(Data); }
  std::size_t get_value_size() { return 100; }
  std::size_t get_key_value_size() { return get_key_size() + get_value_size(); }

private:
  BlockCacheConfig block_cache_config;
  void* key_value_buffer{};
  uint64_t key_value_buffer_size;
  int my_cache_index = 0;
  RDMACacheIndex* cache_index_buffer{};
  std::vector<RDMACacheIndex*> cache_index_buffers;

  std::unique_ptr<std::pmr::monotonic_buffer_resource> key_value_mbr = nullptr;
  std::unique_ptr<std::pmr::unsynchronized_pool_resource> key_value_upr = nullptr;
  std::unique_ptr<std::pmr::polymorphic_allocator<uint8_t>> key_value_pa = nullptr;
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

  using ReadCallback = std::function<void(const KeyType&)>;
  using WriteCallback = std::function<void(const KeyType&, const ValueType&)>;
  using EvictionCallback = std::function<void(const KeyType&, const ValueType&)>;
  void add_callback_on_read(ReadCallback callback) { read_callbacks.emplace_back(callback); }
  void add_callback_on_write(WriteCallback callback) { write_callbacks.emplace_back(callback); }
  void add_callback_on_eviction(EvictionCallback callback) { eviction_callbacks.emplace_back(callback); }

protected:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<BlockDB> block_db;
  std::size_t cache_size;

  uint64_t block_db_num_entries;
  uint64_t block_db_block_size;
  std::vector<ReadCallback> read_callbacks;
  std::vector<WriteCallback> write_callbacks;
  std::vector<EvictionCallback> eviction_callbacks;
};