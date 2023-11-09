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

  block_cache.put("A", "A");
  block_cache.put("B", "B");
  block_cache.put("C", "C");

  info("Got A {}", block_cache.exists_in_cache("A"));
  info("Got B {}", block_cache.exists_in_cache("B"));
  info("Got C {}", block_cache.exists_in_cache("C"));

  // auto random_cache =
  //     RandomCache<std::string, std::string>::InitializeFromConfigFile(
  //         "../config.json");
}
