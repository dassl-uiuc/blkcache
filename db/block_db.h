#pragma once

#include "config.h"
#include "db.h"

#include <assert.h>
#include <list>
#include <unordered_map>

constexpr auto BLOCK_SIZE = 4096u;

class BlockDB : public DB {
public:
  void init(BlockCacheConfig block_cache_config) override {
    DB::init(block_cache_config);

    fd = open(block_cache_config.db.block_db.filename.c_str(),
              O_RDWR | O_DIRECT);
    cache_size = block_cache_config.db.block_db.cache_size;
  }

  tl::expected<void, DBError> put(const std::string &key,
                                  const std::string &value) override {
    if (auto found = key_to_offset.find(key);
        found != std::end(key_to_offset)) {
      return tl::unexpected{DBError::WriteKeyExists};
    }

    char buf[BLOCK_SIZE] __attribute__((__aligned__(BLOCK_SIZE))) = {0};
    strcpy((char *)value.c_str(), buf);
    write(fd, buf, BLOCK_SIZE);
    fsync(fd);
  }

  tl::expected<std::string, DBError> get(const std::string &key) override {
    if (auto found = key_to_offset.find(key);
        found != std::end(key_to_offset)) {
      auto offset = found->second;

      char buf[BLOCK_SIZE] __attribute__((__aligned__(BLOCK_SIZE))) = {0};
      assert(pread(fd, buf, BLOCK_SIZE, offset) == BLOCK_SIZE);
      return buf;
    }
    return tl::unexpected{DBError::KeyDoesNotExist};
  }

  tl::expected<void, DBError> remove(const std::string &key) override {
    return tl::expected<void, DBError>{};
  }

  std::size_t size() const override { return 0; }

private:
  int fd = -1;
  size_t cache_size = 0;
  size_t cursor = 0;
  std::unordered_map<std::string, size_t> key_to_offset;
};
