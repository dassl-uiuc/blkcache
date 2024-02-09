#include <assert.h>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "block_cache.h"

int main(int argc, char **argv) {
  auto block_cache =
      BlockCache<std::string, std::string>::InitializeFromConfigFile(
          "../config.json");

  block_cache.put("A", "A");
  block_cache.put("B", "B");
  block_cache.put("C", "C");
  info("Got {}", block_cache.get("A"));

  auto config = block_cache.get_config();
  config.policy_type = "random";
  block_cache = BlockCache<std::string, std::string>(config);

  block_cache.put("A", "A");
  block_cache.put("B", "B");
  block_cache.put("C", "C");

  info("Got A {}", block_cache.exists_in_cache("A"));
  info("Got B {}", block_cache.exists_in_cache("B"));
  info("Got C {}", block_cache.exists_in_cache("C"));

  config.policy_type = "split";
  block_cache = BlockCache<std::string, std::string>(config);

  // block_cache.put("A", "A");
  // block_cache.put("B", "B");
  // block_cache.put("C", "C");
  // block_cache.put("D", "D");
  // block_cache.put("E", "E");
  // block_cache.put("F", "F");
  // block_cache.put("G", "G");
  // block_cache.put("H", "H");
  bool owning = false;
  for (int i = 0; i < 1; i++)
  {
    char c = 'A' + i;
    block_cache.put(std::string(1, c), std::string(1, c), owning);
  }
  owning = true;
  for (int i = 0; i < 8; i++)
  {
    char c = 'A' + i;
    block_cache.put(std::string(1, c), std::string(1, c), owning);
  }

  for (int i = 0; i < 3; i++)
  {
    char c = 'A' + i;
    // block_cache.get(std::string(1, c));
  }

  block_cache.get_cache()->dump(std::cout);

  config.policy_type = "thread_safe_lru";
  config.ingest_block_index = true;
  config.db.block_db.num_entries = 1024 * 1024;
  auto total_work = 1024 * 8;
  config.cache.thread_safe_lru.cache_size = total_work / 4;
  auto num_threads = 64;
  auto work_per_thread = total_work / num_threads;
  block_cache = BlockCache<std::string, std::string>(config);
  {
    auto workers = std::vector<std::thread>();
    for (int i = 0; i < num_threads; i++) {
      workers.push_back(std::thread([&, i] {
        for (auto j = 0; j < work_per_thread; j++)
        {
          auto key = std::to_string(j + i * work_per_thread);
          auto value = std::to_string(j + i * work_per_thread);
          block_cache.put(key, value);
        }
      }));
    }

    for (auto &worker : workers) {
      worker.join();
    }
    block_cache.get_cache()->dump(std::cout);
  }
  // for (auto i = 0; i < total_work; i++)
  // {
  //   auto key = std::to_string(i);
  //   auto value = std::to_string(i);
  //   block_cache.put(key, value);
  // }
  info("Finished putting {} items", total_work);
  auto timer = std::chrono::high_resolution_clock::now();
  auto workers = std::vector<std::thread>();
  for (int i = 0; i < num_threads; i++) {
    workers.push_back(std::thread([&, i] {
      for (auto j = 0; j < work_per_thread; j++)
      {
        auto key = std::to_string(j + i * work_per_thread);
        auto value = std::to_string(j + i * work_per_thread);
        auto expected_value = block_cache.get(key);
        if (expected_value != value) {
          panic("Expected {} but got {}", value, expected_value);
        }
      }
    }));
  }

  for (auto &worker : workers) {
    worker.join();
  }

  auto elapsed = std::chrono::high_resolution_clock::now() - timer;
  info("Elapsed time: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
  block_cache.get_cache()->dump(std::cout);

  info("AA {}", block_cache.exists_in_cache("1"));


  // auto random_cache =
  //     RandomCache<std::string, std::string>::InitializeFromConfigFile(
  //         "../config.json");
}
