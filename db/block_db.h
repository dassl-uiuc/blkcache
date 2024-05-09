#pragma once

#include "config.h"
#include "db.h"

#include "unordered_dense.h"
#include "concurrentqueue.h"

#include <assert.h>
#include <list>
#include <unordered_map>
#include <liburing.h>
#undef BLOCK_SIZE

constexpr auto BLOCK_DB_SIZE = 4096u;

// #define IO_URING_SUBMITTING_THREAD

class BlockDB : public DB {
public:
  virtual ~BlockDB() {
    if (fd) {
      if (block_cache_config.db.block_db.async)
      {
        for (auto& iouring_worker : iouring_workers)
        {
          iouring_worker->stop = true;
          
          struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);
          io_uring_prep_shutdown(sqe, fd, 0);
          io_uring_submit(&iouring_worker->ring);
        }
        for (auto& iouring_worker : iouring_workers)
        {
#ifdef IO_URING_SUBMITTING_THREAD
          iouring_worker->submitting_thread.join();
#endif
          iouring_worker->waiting_thread.join();
          io_uring_queue_exit(&iouring_worker->ring);

          AsyncReadWriteRequest* async_read_write_request;
          while (iouring_worker->async_read_write_requests.try_dequeue(async_read_write_request))
          {
            for (auto& iovec : async_read_write_request->iovecs)
            {
              free(iovec.iov_base);
            }
            delete async_read_write_request;
          }
        }
      }
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

    const auto &block_size = block_cache_config.db.block_db.block_size;

    num_entries = block_cache_config.db.block_db.num_entries;
    storage_size = num_entries * block_size;
    info("BlockDB num_entries: {}, block_size: {}, storage_size: {}",
         num_entries, block_size, storage_size);

    char *buf = nullptr;
    constexpr std::size_t MAX_POSIX_MEMALIGN_SIZE = 1024u * 1024u * 1024u;
    if (posix_memalign((void **)&buf, block_size, MAX_POSIX_MEMALIGN_SIZE)) {
      perror("posix_memalign");
      exit(EXIT_FAILURE);
    }
    lseek(fd, 0, SEEK_SET);

    // lseek(fd, storage_size, SEEK_SET);
    // write(fd, buf, 1);
    // lseek(fd, 0, SEEK_SET);

    // if (ftruncate(fd, storage_size)) {
    //   perror("ftruncate");
    //   exit(EXIT_FAILURE);
    // }

    // auto remaining = storage_size;
    // while (remaining > 0)
    // {
    //     auto remaining_size = std::min(MAX_POSIX_MEMALIGN_SIZE, remaining);
    //     assert(write(fd, buf, remaining_size) != -1);
    //     remaining -= remaining_size;
    // }
    // fsync(fd);
    free(buf);

    const auto num_iouring_workers = block_cache_config.db.block_db.io_uring_worker_threads;
    iouring_workers.reserve(num_iouring_workers);
    if (block_cache_config.db.block_db.async)
    {
      for (auto i = 0; i < num_iouring_workers; i++)
      {
        auto iouring_worker = std::make_shared<IOURingWorker>();
        
        // Init ring
        io_uring_queue_init(block_cache_config.db.block_db.io_uring_ring_size, &iouring_worker->ring, 0);

        // Init read requests
        for (auto i = 0; i < block_cache_config.db.block_db.io_uring_ring_size; i++)
        {
          auto async_read_write_request = new AsyncReadWriteRequest{};
          auto& iovecs = async_read_write_request->iovecs;
          auto& iovec = iovecs.emplace_back();
          if (posix_memalign(&iovec.iov_base, BLOCK_DB_SIZE, BLOCK_DB_SIZE)) {
            perror("posix_memalign");
            exit(EXIT_FAILURE);
          }
          iovec.iov_len = BLOCK_DB_SIZE;

          iouring_worker->async_read_write_requests.enqueue(async_read_write_request);
        }

        // Submitting thread
#ifdef IO_URING_SUBMITTING_THREAD
        iouring_worker->submitting_thread = std::thread([this, iouring_worker] {
          while (!iouring_worker->stop)
          {
            struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);

            AsyncReadWriteRequest async_submit_read_request;
            while (!iouring_worker->async_read_write_submit_requests.try_dequeue(async_submit_read_request))
            {
              if (iouring_worker->stop)
              {
                break;
              }
              std::this_thread::yield();
            }

            if (iouring_worker->stop)
            {
              break;
            }

            AsyncReadWriteRequest* async_read_write_request;
            while (!iouring_worker->async_read_write_requests.try_dequeue(async_read_write_request))
            {
              panic("No async_read_write_request available!");
            }

            async_read_write_request->key = async_submit_read_request.key;
            async_read_write_request->value = async_submit_read_request.value;
            async_read_write_request->callback = std::move(async_submit_read_request.callback);
            auto offset = async_submit_read_request.iovecs[0].iov_len;
            
            auto& iovecs = async_read_write_request->iovecs;

            if (async_read_write_request->value.empty())
            {
              io_uring_prep_readv(sqe, fd, iovecs.data(), iovecs.size(), offset);
            }
            else
            {
              io_uring_prep_writev(sqe, fd, iovecs.data(), iovecs.size(), offset);
            }
            io_uring_sqe_set_data(sqe, async_read_write_request);
            io_uring_submit(&iouring_worker->ring);            
          }
        });
#endif

        // Waiting thread
        iouring_worker->waiting_thread = std::thread([this, iouring_worker] {
          while (!iouring_worker->stop)
          {
            struct io_uring_cqe *cqe;
            int ret = io_uring_wait_cqe(&iouring_worker->ring, &cqe);
            if (ret < 0)
            {
              info("io_uring_wait_cqe: {}", ret);
              break;
            }

            auto async_read_write_request = reinterpret_cast<AsyncReadWriteRequest *>(io_uring_cqe_get_data(cqe));
            if (!async_read_write_request)
            {
              info("[Background disk thread] Waiting thread: async_read_write_request is null, exiting...");
              break;
            }
            const auto& key = async_read_write_request->key;
            const auto& value = async_read_write_request->value;

            if (value.empty())
            {
              char* buf = reinterpret_cast<char*>(async_read_write_request->iovecs[0].iov_base);

              auto buf_offset = 0;
              auto read_data = [&](auto &v) {
                memcpy(&v, buf + buf_offset, sizeof(v));
                buf_offset += sizeof(v);
              };

              uint32_t avaliable = 0;
              read_data(avaliable);

              if (!avaliable) {
                panic("Key does not exist {}", key);
              }

              std::size_t key_length;
              read_data(key_length);

              std::string_view key_expected(buf + buf_offset, buf + buf_offset + key_length);
              if (key != key_expected) {
                // panic("Key is not expected {} != {}", key, key_expected);
              }

              buf_offset += key_length;

              std::size_t value_length;
              read_data(value_length);

              std::string value(buf + buf_offset, buf + buf_offset + value_length);
              buf_offset += value_length;

              // Callback
              async_read_write_request->callback(value);
            } else {
              async_read_write_request->callback("");
            }

            // Add back to queue
            iouring_worker->async_read_write_requests.enqueue(async_read_write_request);

            io_uring_cqe_seen(&iouring_worker->ring, cqe);
          }
        });

        iouring_workers.emplace_back(iouring_worker);
      }
    }
  }

  uint64_t hash_index(const std::string &s) {
    if (block_cache_config.ingest_block_index) {
      auto index = std::stoull(s);
      if (index > num_entries) {
        panic("Index {} > num_entries {}", index, num_entries);
      }
      return index;
    } else {
      auto index = ankerl::unordered_dense::hash<std::string>{}(s);
      return index % (num_entries - 1);
    }
  }

  uint8_t *get_pointer_to_data_block(std::string &key) override {
    const auto &block_size = block_cache_config.db.block_db.block_size;

    auto index = hash_index(key);
    auto offset = index * block_size;

    return reinterpret_cast<uint8_t *>(offset);
  }

  DBError put(const std::string &key, const std::string &value) override {
    // if (auto found = key_to_offset.find(key); found !=
    // std::end(key_to_offset)) {
    //   return DBError::WriteKeyExists;
    // }
    // std::lock_guard<std::mutex> lock(m);

    const auto &block_size = block_cache_config.db.block_db.block_size;

    char buf[BLOCK_DB_SIZE] __attribute__((__aligned__(BLOCK_DB_SIZE))) = {0};
    auto buf_offset = 0;
    auto copy_data = [&](auto v) {
      memcpy(buf + buf_offset, &v, sizeof(v));
      buf_offset += sizeof(v);
    };

    auto index = hash_index(key);
    auto offset = index * block_size;

    uint32_t avaliable = 1;
    copy_data(avaliable);

    copy_data(key.length());
    memcpy(buf + buf_offset, (char *)key.c_str(), key.length());
    buf_offset += key.length();

    copy_data(value.length());
    memcpy(buf + buf_offset, (char *)value.c_str(), value.length());
    buf_offset += value.length();

    if (buf_offset > block_size) {
      return DBError::WriteOutOfBounds;
    }

    // pwrite(fd, buf, BLOCK_DB_SIZE, offset);
    lseek(fd, offset, SEEK_SET);
    if (write(fd, buf, block_size) == -1) {
      return DBError::WriteFailed;
    }
    // fsync(fd);

    return DBError::None;
  }

  tl::expected<std::string, DBError> get(const std::string &key) override {
    // if (auto found = key_to_offset.find(key);
    //     found != std::end(key_to_offset)) {
    //   auto offset = found->second;

    const auto &block_size = block_cache_config.db.block_db.block_size;

    auto index = hash_index(key);
    auto offset = index * block_size;

    char buf[BLOCK_DB_SIZE] __attribute__((__aligned__(BLOCK_DB_SIZE))) = {0};
    auto result = pread(fd, buf, block_size, offset);
    if (result != block_size) {
      // panic("Read less than result {} < {} at offset {}", result, block_size,
      // offset);
      return tl::unexpected{DBError::KeyDoesNotExist};
    }

    // assert(pread(fd, buf, block_size, offset) == block_size);

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

    std::string_view key_expected(buf + buf_offset, buf + buf_offset + key_length);
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

  AsyncID get_async(const std::string &key, AsyncCallback callback) override {
    if (!block_cache_config.db.block_db.async)
    {
      panic("Async not enabled");
    }

    const auto &block_size = block_cache_config.db.block_db.block_size;

    auto index = hash_index(key);
    auto offset = index * block_size;

    auto id = current_async_id.fetch_add(1, std::memory_order::relaxed);
    auto& iouring_worker = iouring_workers[id % iouring_workers.size()];

#ifdef IO_URING_SUBMITTING_THREAD
    AsyncReadWriteRequest async_read_write_request;

    async_read_write_request.key = key;
    async_read_write_request.callback = std::move(callback);
    async_read_write_request.iovecs.resize(1);
    auto& iovec = async_read_write_request.iovecs[0];
    iovec.iov_len = offset;

    iouring_worker->async_read_write_submit_requests.enqueue(std::move(async_read_write_request));    
#else

    std::lock_guard<std::mutex> lock(iouring_worker->io_uring_lock);
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);

    AsyncReadWriteRequest* async_read_write_request;
    while (!iouring_worker->async_read_write_requests.try_dequeue(async_read_write_request))
    {
      panic("No async_read_write_request available!");
    }

    async_read_write_request->key = key;
    async_read_write_request->callback = std::move(callback);
    auto& iovecs = async_read_write_request->iovecs;

    io_uring_prep_readv(sqe, fd, iovecs.data(), iovecs.size(), offset);
    io_uring_sqe_set_data(sqe, async_read_write_request);
    io_uring_submit(&iouring_worker->ring);
#endif

    return id;
  }

  AsyncID write_async(const std::string &key, const std::string &value, AsyncCallback callback) override {
    if (!block_cache_config.db.block_db.async)
    {
      panic("Async not enabled");
    }

    const auto &block_size = block_cache_config.db.block_db.block_size;

    auto index = hash_index(key);
    auto offset = index * block_size;

    auto id = current_async_id.fetch_add(1, std::memory_order::relaxed);
    auto& iouring_worker = iouring_workers[id % iouring_workers.size()];

#ifdef IO_URING_SUBMITTING_THREAD
    AsyncReadWriteRequest async_read_write_request;

    async_read_write_request.key = key;
    async_read_write_request.value = value;
    async_read_write_request.callback = std::move(callback);
    async_read_write_request.iovecs.resize(1);
    auto& iovec = async_read_write_request.iovecs[0];
    iovec.iov_len = offset;

    iouring_worker->async_read_write_submit_requests.enqueue(std::move(async_read_write_request));    
#else

    std::lock_guard<std::mutex> lock(iouring_worker->io_uring_lock);
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);

    AsyncReadWriteRequest* async_read_write_request;
    while (!iouring_worker->async_read_write_requests.try_dequeue(async_read_write_request))
    {
      panic("No async_read_write_request available!");
    }

    async_read_write_request->key = key;
    async_read_write_request->value = value;
    async_read_write_request->callback = std::move(callback);
    auto& iovecs = async_read_write_request->iovecs;

    io_uring_prep_writev(sqe, fd, iovecs.data(), iovecs.size(), offset);
    io_uring_sqe_set_data(sqe, async_read_write_request);
    io_uring_submit(&iouring_worker->ring);
#endif

    return id;
  }

  DBError remove(const std::string &key) override {
    return DBError::Unimplemented;
  }

  std::size_t size() const override { return 0; }

public:
  struct AsyncReadWriteRequest
  {
    std::string key;
    std::string value;
    AsyncCallback callback;
    std::vector<struct iovec> iovecs;
  };

  struct IOURingWorker
  {
    struct io_uring ring;
    bool stop = false;
    std::thread submitting_thread;
    std::thread waiting_thread;
    std::mutex io_uring_lock;
    moodycamel::ConcurrentQueue<AsyncReadWriteRequest*> async_read_write_requests;
    moodycamel::ConcurrentQueue<AsyncReadWriteRequest> async_read_write_submit_requests;
  };
private:
  std::mutex m;
  std::unordered_map<std::string, int> key_to_offset;
  int fd = -1;
  size_t num_entries = 0;
  size_t storage_size = 0;
  size_t cursor = 0;

  std::atomic<uint64_t> current_async_id{};
  std::vector<std::shared_ptr<IOURingWorker>> iouring_workers;
};
