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

struct ReadWriteConfig {
  int cache_size;
  float read_ratio;
  float write_ratio;
  std::string read_cache;
  std::string write_cache;
};

struct CacheConfig {
  LRUConfig lru;
  RandomCacheConfig random;
  ReadWriteConfig read_write;
};

struct BlockCacheConfig {
  bool ingest_block_index;
  std::string policy_type;
  std::string db_type;
  DBConfig db;
  CacheConfig cache;
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

inline void from_json(const json &j, ReadWriteConfig &read_write) {
  j.at("cache_size").get_to(read_write.cache_size);
  j.at("read_ratio").get_to(read_write.read_ratio);
  j.at("write_ratio").get_to(read_write.write_ratio);
  j.at("read_cache").get_to(read_write.read_cache);
  j.at("write_cache").get_to(read_write.write_cache);
}

inline void from_json(const json &j, CacheConfig &cc) {
  j.at("lru").get_to(cc.lru);
  j.at("random").get_to(cc.random);
  j.at("read_write").get_to(cc.read_write);
}

inline void from_json(const json &j, BlockCacheConfig &bcc) {
  j.at("ingest_block_index").get_to(bcc.ingest_block_index);
  j.at("policy_type").get_to(bcc.policy_type);
  j.at("db_type").get_to(bcc.db_type);
  j.at("db").get_to(bcc.db);
  j.at("cache").get_to(bcc.cache);
}
