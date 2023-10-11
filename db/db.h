#pragma once

#include <string>
#include <vector>

#include "expected.hpp"

enum class DBError { Uninitialized, WriteKeyExists, KeyDoesNotExist };

class DB {
public:
  virtual ~DB() {}
  virtual void init(BlockCacheConfig block_cache_config_) {
    block_cache_config = block_cache_config_;
  }
  virtual void close() {}
  virtual void shutdown() {}

  virtual tl::expected<void, DBError> put(const std::string &key,
                                          const std::string &value) = 0;
  virtual tl::expected<std::string, DBError> get(const std::string &key) = 0;
  virtual tl::expected<void, DBError> remove(const std::string &key) = 0;
  virtual std::size_t size() const = 0;

protected:
  BlockCacheConfig block_cache_config;
};