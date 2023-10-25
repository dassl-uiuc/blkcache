#pragma once

#include "config.h"
#include "db.h"

#include <assert.h>
#include <list>
#include <unordered_map>

constexpr auto BLOCK_SIZE = 4096u;

class BlockDB : public DB {
public:
  virtual ~BlockDB() {
    if (fd) {
      ::close(fd);
    }
  }
  void init(BlockCacheConfig block_cache_config) override {
    DB::init(block_cache_config);

    fd = open(block_cache_config.db.block_db.filename.c_str(),
              O_CREAT | O_RDWR | O_TRUNC | O_DIRECT, S_IRWXU);

    if (fd == -1) {
      perror("open");
      exit(EXIT_FAILURE);
    }

    info("Opened BlockDB: {}", block_cache_config.db.block_db.filename);

    num_entries = block_cache_config.db.block_db.num_entries;
    cache_size = num_entries * BLOCK_SIZE;
    char *buf = nullptr;
    if (posix_memalign((void **)&buf, BLOCK_SIZE, cache_size)) {
      perror("posix_memalign");
      exit(EXIT_FAILURE);
    }

    lseek(fd, 0, SEEK_SET);
    assert(write(fd, buf, cache_size) != -1);
    fsync(fd);
    free(buf);
  }

  uint64_t hash_index(const std::string &s) {
    if (block_cache_config.ingest_block_index) {
      auto index = std::stoull(s);
      if (index > num_entries) {
        panic("Index {} > num_entries {}", index, num_entries);
      }
      return index;
    } else {
      auto index = std::hash<std::string>{}(s);
      return index % num_entries;
    }
  }

  DBError put(const std::string &key, const std::string &value) override {
    // if (auto found = key_to_offset.find(key); found !=
    // std::end(key_to_offset)) {
    //   return DBError::WriteKeyExists;
    // }

    char buf[BLOCK_SIZE] __attribute__((__aligned__(BLOCK_SIZE))) = {0};
    auto buf_offset = 0;
    auto copy_data = [&](auto v) {
      memcpy(buf + buf_offset, &v, sizeof(v));
      buf_offset += sizeof(v);
    };

    auto index = hash_index(key);
    auto offset = index * BLOCK_SIZE;

    uint32_t avaliable = 1;
    copy_data(avaliable);

    copy_data(key.length());
    memcpy(buf + buf_offset, (char *)key.c_str(), key.length());
    buf_offset += key.length();

    copy_data(value.length());
    memcpy(buf + buf_offset, (char *)value.c_str(), value.length());
    buf_offset += value.length();

    if (buf_offset > BLOCK_SIZE) {
      return DBError::WriteOutOfBounds;
    }

    // pwrite(fd, buf, BLOCK_SIZE, offset);
    lseek(fd, offset, SEEK_SET);
    if (write(fd, buf, BLOCK_SIZE) == -1) {
      return DBError::WriteFailed;
    }
    fsync(fd);

    return DBError::None;
  }

  tl::expected<std::string, DBError> get(const std::string &key) override {
    // if (auto found = key_to_offset.find(key);
    //     found != std::end(key_to_offset)) {
    //   auto offset = found->second;

    auto index = hash_index(key);
    auto offset = index * BLOCK_SIZE;

    char buf[BLOCK_SIZE] __attribute__((__aligned__(BLOCK_SIZE))) = {0};
    auto result = pread(fd, buf, BLOCK_SIZE, offset);
    if (result != BLOCK_SIZE) {
      panic("Read less than result {} < {}", result, BLOCK_SIZE);
    }

    // assert(pread(fd, buf, BLOCK_SIZE, offset) == BLOCK_SIZE);

    auto buf_offset = 0;
    auto read_data = [&](auto &v) {
      memcpy(&v, buf + buf_offset, sizeof(v));
      buf_offset += sizeof(v);
    };

    uint32_t avaliable = 0;
    read_data(avaliable);

    if (!avaliable) {
      return tl::unexpected{DBError::KeyDoesNotExist};
    }

    std::size_t key_length;
    read_data(key_length);

    std::string key_expected(buf + buf_offset, buf + buf_offset + key_length);
    if (key != key_expected) {
      return tl::unexpected{DBError::KeyIsNotExpected};
    }

    buf_offset += key_length;

    std::size_t value_length;
    read_data(value_length);

    std::string value(buf + buf_offset, buf + buf_offset + value_length);
    buf_offset += value_length;

    return value;
  }

  DBError remove(const std::string &key) override {
    return DBError::Unimplemented;
  }

  std::size_t size() const override { return 0; }

private:
  std::unordered_map<std::string, int> key_to_offset;
  int fd = -1;
  int num_entries = 0;
  size_t cache_size = 0;
  size_t cursor = 0;
};
