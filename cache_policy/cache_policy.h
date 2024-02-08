#pragma once

#include <cstdint>

#include "config.h"
#include "db/block_db.h"
#include "utils.h"

// #include "infinity/infinity.h"

// constexpr auto DEFAULT_PAGE_SIZE = 4096;

// class RdmaManager {
// public:
//   RdmaManager(BlockCacheConfig block_cache_config_,
//               std::shared_ptr<BlockDB> block_db_, std::size_t cache_size_)
//       : block_cache_config(block_cache_config_), block_db(block_db_),
//         cache_size(cache_size_) {
//     // if (this->block_db_block_size != DEFAULT_PAGE_SIZE)
//     // {
//     //   panic("Block size needs to be page size {} != {}",
//     //   this->block_db_block_size, DEFAULT_PAGE_SIZE);
//     // }
//     // void* mem{};
//     // int res = posix_memalign(&mem, DEFAULT_PAGE_SIZE,
//     // this->block_db_num_entries);

//     context = std::make_unique<infinity::core::Context>(3);
//     qp_factory =
//         std::make_unique<infinity::queues::QueuePairFactory>(context.get());
//     infinity::queues::QueuePair *qp;

//     printf("Creating buffers to read from and write to\n");
//     shared_memory = std::make_unique<infinity::memory::Buffer>(
//         context.get(), 64 * 1024 * 1024 * sizeof(char));
//     infinity::memory::RegionToken *buffer_token =
//         shared_memory->createRegionToken();

//     printf("Creating buffers to receive a message\n");
//     internal_receiving_buffer = std::make_unique<infinity::memory::Buffer>(
//         context.get(), 128 * sizeof(char));
//     context->postReceiveBuffer(&*internal_receiving_buffer);

//     auto server_thread = [&](auto machine_config) { machine_config.ip; };

//     for (const auto &machine_config :
//          this->block_cache_config.remote_machine_configs) {
//       servers.push_back(std::thread(server_thread, machine_config));
//       machine_config.ip;
//     }

//     auto num_connections =
//         2 * this->block_cache_config.remote_machine_configs.size();
//     for (auto i = 0; i < num_connections; i++) {
//     }
//   }

// private:
//   BlockCacheConfig block_cache_config;
//   std::shared_ptr<BlockDB> block_db;
//   std::vector<std::thread> servers;
//   std::vector<std::thread> clients;

//   std::unique_ptr<infinity::core::Context> context;
//   std::unique_ptr<infinity::queues::QueuePairFactory> qp_factory;

//   std::unique_ptr<infinity::memory::Buffer> shared_memory;
//   std::unique_ptr<infinity::memory::Buffer> internal_receiving_buffer;
//   std::size_t cache_size;
// };

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

protected:
  BlockCacheConfig block_cache_config;
  std::shared_ptr<BlockDB> block_db;
  std::size_t cache_size;

  uint64_t block_db_num_entries;
  uint64_t block_db_block_size;
};