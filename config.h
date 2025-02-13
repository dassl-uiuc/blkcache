#pragma once

#include <string>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "utils.h"

struct BlockDBConfig {
  std::string filename;
  std::string copied_filename;
  int num_entries;
  int block_size;
  bool async;
  uint64_t io_uring_ring_size;
  uint64_t io_uring_worker_threads;
  uint64_t io_uring_write_ring_size;
  uint64_t io_uring_write_worker_threads;
  uint64_t batch_write_size;
  uint64_t batch_max_pending_requests;
  uint64_t milliseconds_flush_dirty_cache;
  uint64_t async_request_threads;
};

struct DBConfig {
  BlockDBConfig block_db;
};

struct LRUConfig {
  int cache_size;
};

struct RandomCacheConfig {
  int cache_size;
};

struct SplitCacheConfig {
  int cache_size;
  float owning_ratio;
  float nonowning_ratio;
  std::string owning_cache_type;
  std::string nonowning_cache_type;
};

struct ThreadSafeLRUConfig {
  int cache_size;
};

struct RdmaConfig {
  int context_index;
};

struct CacheConfig {
  LRUConfig lru;
  RandomCacheConfig random;
  SplitCacheConfig split;
  ThreadSafeLRUConfig thread_safe_lru;
  bool paged;
  RdmaConfig rdma;
};

struct RemoteMachineConfig {
  uint64_t index;
  std::string ip;
  uint64_t port;
  bool server;
  bool shared_log;
};

struct Baseline {
  std::string selected;
  bool one_sided_rdma_enabled;
  bool use_cache_indexing;
};

struct BlockCacheConfig {
  bool ingest_block_index;
  std::string policy_type;
  uint64_t rdma_port;
  bool craq_enabled;
  std::string db_type;
  DBConfig db;
  CacheConfig cache;
  Baseline baseline;
  uint64_t access_rate;
  uint64_t access_per_itr;
  std::vector<RemoteMachineConfig> remote_machine_configs;
  RemoteMachineConfig shared_log_remote_machine_config;
};

inline void from_json(const json &j, BlockDBConfig &block_db) {
  j.at("filename").get_to(block_db.filename);
  if (j.contains("copied_filename")) {
    j.at("copied_filename").get_to(block_db.copied_filename);
  }
  j.at("num_entries").get_to(block_db.num_entries);
  j.at("block_size").get_to(block_db.block_size);
  j.at("async").get_to(block_db.async);
  j.at("io_uring_ring_size").get_to(block_db.io_uring_ring_size);
  j.at("io_uring_worker_threads").get_to(block_db.io_uring_worker_threads);
  if (j.contains("io_uring_write_ring_size")) {
    j.at("io_uring_write_ring_size").get_to(block_db.io_uring_write_ring_size);
  } else {
    block_db.io_uring_write_ring_size = 128;
  }
  if (j.contains("io_uring_write_worker_threads")) {
    j.at("io_uring_write_worker_threads").get_to(block_db.io_uring_write_worker_threads);
  } else {
    block_db.io_uring_write_worker_threads = 0;
  }
  if (j.contains("batch_write_size")) {
    j.at("batch_write_size").get_to(block_db.batch_write_size);
  }
  if (j.contains("batch_max_pending_requests")) {
    j.at("batch_max_pending_requests").get_to(block_db.batch_max_pending_requests);
  }
  if (j.contains("milliseconds_flush_dirty_cache")) {
    j.at("milliseconds_flush_dirty_cache").get_to(block_db.milliseconds_flush_dirty_cache);
  }
  if (j.contains("async_request_threads")) {
    j.at("async_request_threads").get_to(block_db.async_request_threads);
  }
}

inline void from_json(const json &j, DBConfig &db) {
  j.at("block_db").get_to(db.block_db);
}

inline void from_json(const json &j, LRUConfig &lru) {
  j.at("cache_size").get_to(lru.cache_size);
}

inline void from_json(const json &j, RandomCacheConfig &random) {
  j.at("cache_size").get_to(random.cache_size);
}

inline void from_json(const json &j, SplitCacheConfig &split) {
  j.at("cache_size").get_to(split.cache_size);
  j.at("owning_ratio").get_to(split.owning_ratio);
  j.at("nonowning_ratio").get_to(split.nonowning_ratio);
  j.at("owning_cache_type").get_to(split.owning_cache_type);
  j.at("nonowning_cache_type").get_to(split.nonowning_cache_type);
}

inline void from_json(const json &j, ThreadSafeLRUConfig &lru) {
  j.at("cache_size").get_to(lru.cache_size);
}

inline void from_json(const json &j, RdmaConfig &rdma) {
  j.at("context_index").get_to(rdma.context_index);
}

inline void from_json(const json &j, CacheConfig &cc) {
  j.at("lru").get_to(cc.lru);
  j.at("random").get_to(cc.random);
  j.at("split").get_to(cc.split);
  j.at("thread_safe_lru").get_to(cc.thread_safe_lru);
  j.at("paged").get_to(cc.paged);
  j.at("rdma").get_to(cc.rdma);
}

inline void from_json(const json &j, RemoteMachineConfig &rmc) {
  j.at("index").get_to(rmc.index);
  j.at("ip").get_to(rmc.ip);
  j.at("port").get_to(rmc.port);
  j.at("server").get_to(rmc.server);
  if (j.contains("shared_log")) {
    j.at("shared_log").get_to(rmc.shared_log);
  }
}

inline void from_json(const json &j, Baseline &baseline) {
  j.at("selected").get_to(baseline.selected);
  j.at("one_sided_rdma_enabled").get_to(baseline.one_sided_rdma_enabled);
  j.at("use_cache_indexing").get_to(baseline.use_cache_indexing);
}

inline void from_json(const json &j, BlockCacheConfig &bcc) {
  j.at("ingest_block_index").get_to(bcc.ingest_block_index);
  if (j.contains("craq_enabled")) {
    j.at("craq_enabled").get_to(bcc.craq_enabled);
  } else {
    bcc.craq_enabled = false;
  }
  j.at("policy_type").get_to(bcc.policy_type);
  j.at("rdma_port").get_to(bcc.rdma_port);
  j.at("db_type").get_to(bcc.db_type);
  j.at("db").get_to(bcc.db);
  j.at("cache").get_to(bcc.cache);
  j.at("baseline").get_to(bcc.baseline);
  j.at("remote_machines").get_to(bcc.remote_machine_configs);
  if (j.contains("access_rate")) {
        j.at("access_rate").get_to(bcc.access_rate);
    } else {
        bcc.access_rate = 1;
    }

    if (j.contains("access_per_itr")) {
        j.at("access_per_itr").get_to(bcc.access_per_itr);
    } else {
        bcc.access_per_itr = 1000000;
    }
}
