#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "config.h"
#include "db/block_db.h"
#include "utils.h"
#include "thread_safe_lru/common.h"

// RDMA related, wrong to be put here but oh well
#define CACHE_INDEX_INVALID (uintptr_t)-1
#define KEY_VALUE_PTR_INVALID -1
// inline static RDMACacheIndex InvalidRDMACacheIndex = RDMACacheIndex{ CACHE_INDEX_INVALID, false, 0 };
#define InvalidRDMACacheIndex RDMACacheIndex{ CACHE_INDEX_INVALID, false, 0 }

struct KeyValue
{
  uint64_t* key;
  std::span<uint8_t> value;
};

struct RDMACacheIndex
{
  uintptr_t key_value_ptr_offset;
  bool isSingleton;
  uint64_t forward_count;
};

struct RDMAKeyValueStorage
{
  struct Data
  {
    uint64_t key;
  };

  RDMAKeyValueStorage(BlockCacheConfig block_cache_config_) :
    block_cache_config(block_cache_config_)
  {
    // key_value_buffer_size = 1024 * 1024 * 1024;
    key_value_buffer_size = block_cache_config.db.block_db.num_entries * get_key_value_size();

    key_value_buffer = std::malloc(key_value_buffer_size);
    std::memset(key_value_buffer, KEY_VALUE_PTR_INVALID, key_value_buffer_size);
    key_value_mbr = std::make_unique<std::pmr::monotonic_buffer_resource>(key_value_buffer, key_value_buffer_size);
    key_value_upr = std::make_unique<std::pmr::synchronized_pool_resource>(key_value_mbr.get());
    key_value_pa = std::make_unique<std::pmr::polymorphic_allocator<uint8_t>>(key_value_upr.get());

    // Initialize cache index
    cache_index_buffer = allocate_cache_index();
  }

  auto get_allocated_cache_index_size()
  {
    auto storage_num_entries = block_cache_config.db.block_db.num_entries + 1; 
    return storage_num_entries * sizeof(RDMACacheIndex);
  }

  RDMACacheIndex* check_oldest_non_singleton() {
        for (int i = 0; i < block_cache_config.db.block_db.num_entries; ++i) {
            RDMACacheIndex& index = cache_index_buffer[i];
            if (!index.isSingleton && index.forward_count > 0) {
                return &index;  // Return a pointer to the non-singleton cache index
            }
        }
        return nullptr;  // Return nullptr if no non-singleton entry is found
    }

  RDMACacheIndex* allocate_cache_index()
  {
    auto size = get_allocated_cache_index_size();
    auto buffer = reinterpret_cast<RDMACacheIndex*>(std::malloc(size));
    std::memset(buffer, CACHE_INDEX_INVALID, size);
    return buffer;
  }

  KeyValue allocate(uint64_t key_index)
  {
    // key value
    // auto ptr = key_value_pa->allocate(get_key_value_size());
    auto ptr = (uint8_t*)key_value_buffer + (get_key_value_size() * key_index);

    // Initialize key
    uint64_t* key = reinterpret_cast<uint64_t*>(ptr);
    *key = key_index;

    // Initialize in cache index
    auto key_value_ptr_offset = (uintptr_t)((uint8_t*)ptr - (uint8_t*)key_value_buffer);
    bool isSingleton = false;
    uint64_t forword_count = 0;
    if (key_value_ptr_offset > key_value_buffer_size)
    {
      panic("[RDMACacheIndex] Out of memory {} > {}!", key_value_ptr_offset, key_value_buffer_size);
    }
    cache_index_buffer[key_index] = RDMACacheIndex{ key_value_ptr_offset, isSingleton, forword_count};
    // info("WRITE BUFFER {} {} {} {}", (void*)cache_index_buffer, key_index, (void*)&cache_index_buffer[key_index], cache_index_buffer[key_index].key_value_ptr_offset);
    // Initialize value
    std::span<uint8_t> value = std::span<uint8_t>(ptr + sizeof(uint64_t), get_key_value_size() - sizeof(uint64_t));

    auto key_value = KeyValue{ key, value };
    return key_value;
  }

  void deallocate(KeyValue key_value)
  {
    cache_index_buffer[*key_value.key] = InvalidRDMACacheIndex;
    // info("DEALLOC {} {}", (void*)key_value.key, (uint64_t)*key_value.key);
    // key_value_pa->deallocate((uint8_t*)key_value.key, get_key_value_size());
    auto ptr = (uint8_t*)key_value_buffer + (get_key_value_size() * *key_value.key);
    memset(ptr, CACHE_INDEX_INVALID, get_key_value_size());

    *key_value.key = KEY_VALUE_PTR_INVALID;
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
  
  KeyValue get_key_value(uint64_t key_index)
  {
    auto ptr = (uint8_t*)key_value_buffer + (get_key_value_size() * key_index);
    auto* key = (uint64_t*)ptr;
    std::span<uint8_t> value = std::span<uint8_t>(ptr + sizeof(uint64_t), get_key_value_size() - sizeof(uint64_t));

    auto key_value = KeyValue{ key, value };
    return key_value;
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
  std::unique_ptr<std::pmr::synchronized_pool_resource> key_value_upr = nullptr;
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
  virtual void* put_nchance(const KeyType &key, const ValueType &val,
           bool owning = false) { panic("Unsupported"); }
  virtual bool delete_key(const KeyType &key) { panic("Unsupported"); }
  virtual void put_singleton(const KeyType &key, const ValueType &val, 
           bool isSingleton, int forward_count,
           bool owning = false) { panic("Unsupported"); }
  virtual bool put_access_rate_match(const KeyType &key, const ValueType &val,
           bool owning = false) { panic("Unsupported"); }
  virtual ValueType get(const KeyType &key) = 0;
  virtual bool exist(const KeyType &key) = 0;
  virtual void remove(const KeyType &key) = 0;
  virtual void dump(std::ostream &os) = 0;
  virtual RDMAKeyValueStorage* get_rdma_key_value_storage() { return nullptr; }

  using ReadCallback = std::function<void(const KeyType&)>;
  using WriteCallback = std::function<void(const KeyType&, const ValueType&)>;
  void add_callback_on_read(ReadCallback callback) { read_callbacks.emplace_back(callback); }
  void add_callback_on_write(WriteCallback callback) { write_callbacks.emplace_back(callback); }
  virtual void add_callback_on_eviction(EvictionCallback<KeyType, ValueType> callback) { eviction_callbacks.emplace_back(callback); }

protected:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<BlockDB> block_db;
  std::size_t cache_size;

  uint64_t block_db_num_entries;
  uint64_t block_db_block_size;
  std::vector<ReadCallback> read_callbacks;
  std::vector<WriteCallback> write_callbacks;
  std::vector<EvictionCallback<KeyType, ValueType>> eviction_callbacks;
};