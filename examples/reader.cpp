#include <assert.h>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#define BLKSZ 4096

#include "block_cache.h"

int main(int argc, char **argv) {
  auto block_cache =
      BlockCache<std::string, std::string>::InitializeFromConfigFile(
          "../config.json");

  block_cache.put("A", "A");
  block_cache.put("B", "B");
  block_cache.put("C", "C");
  info("Got {}", block_cache.get("A"));

  // int fd = open("foo", O_RDWR | O_DIRECT);
  // assert(fd);
  // int cache_perc = atoi(argv[1]);

  // uint64_t num_blks = 10000;
  // uint64_t num_ops = 10 * num_blks;
  // float cp = num_blks * (cache_perc / 100.0);
  // uint64_t cache_size = (uint64_t)cp;
  // // std::cout << "Cache size:"<<  cache_perc <<"%; Absolute size:" <<
  // // cache_size << std::endl;
  // auto cache = new LRUCache<uint64_t, std::string>(cache_size);
  // auto start = std::chrono::high_resolution_clock::now();

  // uint64_t i = 0;
  // uint64_t cache_misses = 0;
  // static char buf[BLKSZ] __attribute__((__aligned__(BLKSZ)));
  // srand(time(NULL));
  // while (i++ < num_ops) {
  //   uint64_t blk_read = rand() % num_blks;
  //   if (cache->exist(blk_read)) {
  //     auto ret = cache->get(blk_read);
  //   } else {
  //     // std::cout << "Reading..."<<blk_read<<std::endl;
  //     assert(pread(fd, buf, BLKSZ, blk_read * BLKSZ) == BLKSZ);
  //     std::string contents(buf, BLKSZ);
  //     cache->put(blk_read, contents);
  //     cache_misses++;
  //   }
  // }

  // auto elapsed = std::chrono::high_resolution_clock::now() - start;

  // long long microseconds =
  //     std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  // std::cout << cache_perc << "%\t" << microseconds / 1000.0 << "\t"
  //           << cache_misses << std::endl;
}
