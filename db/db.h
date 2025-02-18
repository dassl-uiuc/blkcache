#pragma once

#include <string>
#include <vector>

#include "expected.hpp"

enum class DBError {
  None,
  Uninitialized,
  Unimplemented,
  WriteKeyExists,
  KeyDoesNotExist,
  WriteOutOfBounds,
  KeyIsNotExpected,
  WriteFailed,
};

using AsyncID = uint64_t;
using AsyncCallback = std::function<void(std::string)>;

class DB {
public:
  virtual ~DB() {}
  virtual void init(BlockCacheConfig block_cache_config_) {
    block_cache_config = block_cache_config_;
  }
  virtual void close() {}
  virtual void shutdown() {}

  virtual uint8_t *get_pointer_to_data_block(std::string &key) = 0;
  virtual DBError put(const std::string &key, const std::string &value) = 0;
  virtual tl::expected<std::string, DBError> get(const std::string &key) = 0;
  virtual AsyncID get_async(const std::string &key, AsyncCallback callback) = 0;
  virtual AsyncID put_async(const std::string &key, const std::string &value, AsyncCallback callback) = 0;
  virtual AsyncID get_async_submit(const std::string &key, AsyncCallback callback) = 0;
  virtual AsyncID put_async_submit(const std::string &key, const std::string &value, AsyncCallback callback) = 0;
  virtual DBError remove(const std::string &key) = 0;
  virtual std::size_t size() const = 0;
  virtual void set_batch_max_pending_requests(std::size_t v) = 0;

public:
  uint64_t writes_blocked_count = 0;
  uint64_t writes_blocked_ns = 0;
protected:
  BlockCacheConfig block_cache_config;
};