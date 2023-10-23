#pragma once

#include <string>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "utils.h"

struct BlockDBConfig {
  std::string filename;
  int num_entries;
};

struct DBConfig {
  BlockDBConfig block_db;
};

struct LRUConfig {
  int cache_size;
};

struct CacheConfig {
  LRUConfig lru;
};

struct BlockCacheConfig {
  std::string policy_type;
  std::string db_type;
  DBConfig db;
  CacheConfig cache;
};

void from_json(const json &j, BlockDBConfig &block_db) {
  j.at("filename").get_to(block_db.filename);
  j.at("num_entries").get_to(block_db.num_entries);
}

void from_json(const json &j, DBConfig &db) {
  j.at("block_db").get_to(db.block_db);
}

void from_json(const json &j, LRUConfig &lru) {
  j.at("cache_size").get_to(lru.cache_size);
}

void from_json(const json &j, CacheConfig &cc) { j.at("lru").get_to(cc.lru); }

void from_json(const json &j, BlockCacheConfig &bcc) {
  j.at("policy_type").get_to(bcc.policy_type);
  j.at("db_type").get_to(bcc.db_type);
  j.at("db").get_to(bcc.db);
  j.at("cache").get_to(bcc.cache);
}
