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

  config.policy_type = "read_write";
  block_cache = BlockCache<std::string, std::string>(config);

  // block_cache.put("A", "A");
  // block_cache.put("B", "B");
  // block_cache.put("C", "C");
  // block_cache.put("D", "D");
  // block_cache.put("E", "E");
  // block_cache.put("F", "F");
  // block_cache.put("G", "G");
  // block_cache.put("H", "H");
  for (int i = 0; i < 8; i++)
  {
    char c = 'A' + i;
    block_cache.put(std::string(1, c), std::string(1, c));
  }

  for (int i = 0; i < 3; i++)
  {
    char c = 'A' + i;
    block_cache.get(std::string(1, c));
  }

  block_cache.get_cache()->dump(std::cout);

  // auto random_cache =
  //     RandomCache<std::string, std::string>::InitializeFromConfigFile(
  //         "../config.json");
}
