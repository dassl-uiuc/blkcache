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

constexpr auto IO_VEC_ALLOCATION_SIZE = 1;
constexpr auto IO_VEC_DEFAULT_SIZE = 1;
constexpr auto IO_VEC_WRITE_SIZE = IO_VEC_ALLOCATION_SIZE;

struct AsyncRequest
{
  std::string key;
  std::string value;
  bool is_read;
  AsyncCallback async_callback;
};

struct AsyncReadWriteRequest
{
  std::string key;
  std::string value;
  AsyncCallback callback;
  std::vector<struct iovec> iovecs;
};

class BlockDB : public DB {
public:
  virtual ~BlockDB() {
    if (fd) {
      if (block_cache_config.db.block_db.async)
      {
        for (auto& async_io_submit_worker : async_io_submit_workers)
        {
          async_io_submit_worker->stop = true;
          async_io_submit_worker->async_request_thread.join();
        }
        for (auto& iouring_worker : iouring_workers)
        {
          iouring_worker->stop = true;
          
          std::lock_guard<std::mutex> lock(iouring_worker->io_uring_lock);
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
        for (auto& iouring_worker : iouring_write_workers)
        {
          iouring_worker->stop = true;
          
          std::lock_guard<std::mutex> lock(iouring_worker->io_uring_lock);
          struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);
          io_uring_prep_shutdown(sqe, fd, 0);
          io_uring_submit(&iouring_worker->ring);
        }
        for (auto& iouring_worker : iouring_write_workers)
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

    auto make_iouring_worker = [&](bool is_write)
    {
      auto iouring_worker = std::make_shared<IOURingWorker>();
      
      // Init ring
      io_uring_params params{};
      // params.flags |= IORING_SETUP_IOPOLL;
      // params.flags |= IORING_SETUP_SINGLE_ISSUER;
      // params.flags |= IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
      // params.flags |= IORING_SETUP_SQPOLL;
      // params.sq_thread_idle = 2000;
      if (is_write)
      {
        io_uring_queue_init_params(block_cache_config.db.block_db.io_uring_write_ring_size, &iouring_worker->ring, &params);
      }
      else
      {
        io_uring_queue_init_params(block_cache_config.db.block_db.io_uring_ring_size, &iouring_worker->ring, &params);
      }
      // io_uring_register_files(&iouring_worker->ring, &fd, 1); // required for sq polling

      // Init read requests
      // for (auto i = 0; i < block_cache_config.db.block_db.io_uring_ring_size; i++)
      for (auto i = 0; i < 1024 * 256; i++)
      {
        auto async_read_write_request = new AsyncReadWriteRequest{};
        auto& iovecs = async_read_write_request->iovecs;
        iovecs.resize(IO_VEC_ALLOCATION_SIZE);
        for (auto& iovec : iovecs)
        {
          if (posix_memalign(&iovec.iov_base, BLOCK_DB_SIZE, BLOCK_DB_SIZE)) {
            perror("posix_memalign");
            exit(EXIT_FAILURE);
          }
          iovec.iov_len = BLOCK_DB_SIZE;
        }

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
            io_uring_prep_readv(sqe, fd, iovecs.data(), IO_VEC_DEFAULT_SIZE, offset);
          }
          else
          {
            io_uring_prep_writev(sqe, fd, iovecs.data(), IO_VEC_DEFAULT_SIZE, offset);
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

            // if (!avaliable) {
            //   panic("Key does not exist {}", key);
            // }

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
            // async_read_write_request->callback("");
          }

          // Add back to queue
          iouring_worker->async_read_write_requests.enqueue(async_read_write_request);

          io_uring_cqe_seen(&iouring_worker->ring, cqe);
        }
      });
      return iouring_worker;
    };

    if (block_cache_config.db.block_db.async)
    {
      for (auto i = 0; i < block_cache_config.db.block_db.io_uring_worker_threads; i++)
      {
        auto iouring_worker = make_iouring_worker(false);
        iouring_workers.emplace_back(iouring_worker);
      }

      io_uring_write_worker_threads = block_cache_config.db.block_db.io_uring_write_worker_threads;
      for (auto i = 0; i < io_uring_write_worker_threads; i++)
      {
        auto iouring_worker = make_iouring_worker(true);
        iouring_write_workers.emplace_back(iouring_worker);
      }

      auto NUM_ASYNC_REQUEST_THREADS = block_cache_config.db.block_db.async_request_threads;
      async_io_submit_workers.reserve(NUM_ASYNC_REQUEST_THREADS);
      for (auto i = 0; i < NUM_ASYNC_REQUEST_THREADS; i++)
      {
        auto async_io_submit_worker = std::make_shared<AsyncIOSubmitWorker>();
        async_io_submit_worker->async_request_thread = std::thread([&, async_io_submit_worker, batch_write_size = block_cache_config.db.block_db.batch_write_size]()
        {
          auto batch_write_current_size = 0;
          while (!async_io_submit_worker->stop)
          {
            AsyncRequest async_request;
            while (!async_io_submit_worker->async_request_queue.try_dequeue(async_request))
            {
              if (async_io_submit_worker->stop)
              {
                break;
              }
              std::this_thread::yield();
            }
            const auto& key = async_request.key;
            const auto& value = async_request.value;
            const auto& is_read = async_request.is_read;
            const auto& async_callback = async_request.async_callback;
            if (is_read)
            {
              this->get_async(key, std::move(async_callback));
            }
            else
            {
              if (batch_write_size > 0)
              {
                const auto &block_size = block_cache_config.db.block_db.block_size;

                auto index = hash_index(key);
                auto offset = index * block_size;

                AsyncID id = 0;
                std::shared_ptr<IOURingWorker> iouring_worker;
                if (io_uring_write_worker_threads > 0)
                {
                  id = current_async_write_id.fetch_add(1, std::memory_order::relaxed);
                  iouring_worker = iouring_write_workers[id % iouring_write_workers.size()];
                }
                else
                {
                  id = current_async_id.fetch_add(1, std::memory_order::relaxed);
                  iouring_worker = iouring_workers[id % iouring_workers.size()];
                }

                AsyncReadWriteRequest* async_read_write_request = get_async_read_write_request(iouring_worker);
                async_read_write_request->key = key;
                async_read_write_request->value = value;
                async_read_write_request->callback = std::move(async_callback);
                auto& iovecs = async_read_write_request->iovecs;

                // std::lock_guard<std::mutex> lock(iouring_worker->io_uring_lock);
                struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);

                io_uring_prep_writev(sqe, fd, iovecs.data(), IO_VEC_WRITE_SIZE, offset);
                io_uring_sqe_set_data(sqe, async_read_write_request);
                batch_write_current_size++;

                if (batch_write_current_size >= batch_write_size || async_io_submit_worker->stop)
                {
                  io_uring_submit(&iouring_worker->ring);
                  batch_write_current_size = 0;
                }
              }
              else
              {
                this->put_async(key, value, std::move(async_callback));
              }
            }
          }
        });

        async_io_submit_workers.emplace_back(async_io_submit_worker);
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

  AsyncReadWriteRequest* get_async_read_write_request(std::shared_ptr<IOURingWorker> iouring_worker)
  {
    AsyncReadWriteRequest* async_read_write_request;
    while (!iouring_worker->async_read_write_requests.try_dequeue(async_read_write_request))
    {
      // info("No async_read_write_request available! - Batch");
      async_read_write_request = new AsyncReadWriteRequest{};
      auto& iovecs = async_read_write_request->iovecs;
      iovecs.resize(IO_VEC_ALLOCATION_SIZE);
      for (auto& iovec : iovecs)
      {
        if (posix_memalign(&iovec.iov_base, BLOCK_DB_SIZE, BLOCK_DB_SIZE)) {
          perror("posix_memalign");
          exit(EXIT_FAILURE);
        }
        iovec.iov_len = BLOCK_DB_SIZE;
      }

      break;
    }
    return async_read_write_request;
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

    AsyncReadWriteRequest* async_read_write_request = get_async_read_write_request(iouring_worker);
    async_read_write_request->key = key;
    async_read_write_request->value = {};
    async_read_write_request->callback = std::move(callback);
    auto& iovecs = async_read_write_request->iovecs;

    std::lock_guard<std::mutex> lock(iouring_worker->io_uring_lock);
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);

    io_uring_prep_readv(sqe, fd, iovecs.data(), IO_VEC_DEFAULT_SIZE, offset);
    io_uring_sqe_set_data(sqe, async_read_write_request);
    io_uring_submit(&iouring_worker->ring);
#endif

    return id;
  }

  AsyncID put_async(const std::string &key, const std::string &value, AsyncCallback callback) override {
    if (!block_cache_config.db.block_db.async)
    {
      panic("Async not enabled");
    }

    const auto &block_size = block_cache_config.db.block_db.block_size;

    auto index = hash_index(key);
    auto offset = index * block_size;

    AsyncID id = 0;
    std::shared_ptr<IOURingWorker> iouring_worker;
    if (io_uring_write_worker_threads > 0)
    {
      id = current_async_write_id.fetch_add(1, std::memory_order::relaxed);
      iouring_worker = iouring_write_workers[id % iouring_write_workers.size()];
    }
    else
    {
      id = current_async_id.fetch_add(1, std::memory_order::relaxed);
      iouring_worker = iouring_workers[id % iouring_workers.size()];
    }

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

    AsyncReadWriteRequest* async_read_write_request = get_async_read_write_request(iouring_worker);
    async_read_write_request->key = key;
    async_read_write_request->value = value;
    async_read_write_request->callback = std::move(callback);
    auto& iovecs = async_read_write_request->iovecs;

    info("LOCK");
    std::lock_guard<std::mutex> lock(iouring_worker->io_uring_lock);
    info("UNLOCK");
    // struct io_uring_sqe *sqe = io_uring_get_sqe(&iouring_worker->ring);

    // io_uring_prep_writev(sqe, fd, iovecs.data(), IO_VEC_DEFAULT_SIZE, offset);
    // io_uring_sqe_set_data(sqe, async_read_write_request);
    // io_uring_submit_and_wait(&iouring_worker->ring, 0);
#endif

    return id;
  }

  AsyncID async_submit(AsyncRequest async_request) {
    auto id = current_async_submit_id.fetch_add(1, std::memory_order::relaxed);
    auto& async_io_submit_worker = async_io_submit_workers[id % async_io_submit_workers.size()];

    async_io_submit_worker->async_request_queue.enqueue(async_request);
    return id;
  }

  AsyncID get_async_submit(const std::string &key, AsyncCallback callback) override {
    if (block_cache_config.db.block_db.async_request_threads > 0)
    {
      auto is_read = true;
      AsyncRequest async_request{key, {}, is_read, callback};
      return async_submit(async_request);
    }
    return get_async(key, callback);
  }

  AsyncID put_async_submit(const std::string &key, const std::string &value, AsyncCallback callback) override {
    if (block_cache_config.db.block_db.async_request_threads > 0)
    {
      auto is_read = false;
      AsyncRequest async_request{key, value, is_read, callback};
      return async_submit(async_request);
    }
    return put_async(key, value, callback);
  }

  DBError remove(const std::string &key) override {
    return DBError::Unimplemented;
  }

  std::size_t size() const override { return 0; }

public:
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

  struct AsyncIOSubmitWorker
  {
    bool stop = false;
    moodycamel::ConcurrentQueue<AsyncRequest> async_request_queue;
    std::thread async_request_thread;
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
  std::atomic<uint64_t> current_async_write_id{};
  std::vector<std::shared_ptr<IOURingWorker>> iouring_write_workers;
  std::atomic<uint64_t> current_async_submit_id{};
  std::vector<std::shared_ptr<AsyncIOSubmitWorker>> async_io_submit_workers;
  int io_uring_write_worker_threads = 0;
};
