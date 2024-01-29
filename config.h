#pragma once

#include <string>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "utils.h"

struct BlockDBConfig {
  std::string filename;
  int num_entries;
  int block_size;
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

struct RdmaConfig {
  int context_index;
};

struct CacheConfig {
  LRUConfig lru;
  RandomCacheConfig random;
  SplitCacheConfig split;
  bool paged;
  RdmaConfig rdma;
};

struct RemoteMachineConfig {
  uint64_t index;
  std::string ip;
  uint64_t port;
};

struct BlockCacheConfig {
  bool ingest_block_index;
  std::string policy_type;
  std::string db_type;
  DBConfig db;
  CacheConfig cache;
  std::vector<RemoteMachineConfig> remote_machine_configs;
};

inline void from_json(const json &j, BlockDBConfig &block_db) {
  j.at("filename").get_to(block_db.filename);
  j.at("num_entries").get_to(block_db.num_entries);
  j.at("block_size").get_to(block_db.block_size);
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

inline void from_json(const json &j, RdmaConfig &rdma) {
  j.at("context_index").get_to(rdma.context_index);
}

inline void from_json(const json &j, CacheConfig &cc) {
  j.at("lru").get_to(cc.lru);
  j.at("random").get_to(cc.random);
  j.at("split").get_to(cc.split);
  j.at("paged").get_to(cc.paged);
  j.at("rdma").get_to(cc.rdma);
}

inline void from_json(const json &j, RemoteMachineConfig &rmc) {
  j.at("index").get_to(rmc.index);
  j.at("ip").get_to(rmc.ip);
  j.at("port").get_to(rmc.port);
}

inline void from_json(const json &j, BlockCacheConfig &bcc) {
  j.at("ingest_block_index").get_to(bcc.ingest_block_index);
  j.at("policy_type").get_to(bcc.policy_type);
  j.at("db_type").get_to(bcc.db_type);
  j.at("db").get_to(bcc.db);
  j.at("cache").get_to(bcc.cache);
  j.at("remote_machines").get_to(bcc.remote_machine_configs);
}
