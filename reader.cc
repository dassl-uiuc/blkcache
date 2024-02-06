#include <iostream>
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <random>

#include "cache.hpp"
#include "fifo_cache_policy.hpp"
#include "zipfian_distribution.h"

#define BLKSZ 4096

template <typename Key, typename Value>
using fifo_cache_t = typename caches::fixed_sized_cache<Key, Value, caches::FIFOCachePolicy>;

int main(int argc, char **argv)
{
	int cache_perc = atoi(argv[1]);
	int num_blks = atoi(argv[2]);
	std::string file_name(argv[3]);
	int fd = open(file_name.c_str(), O_RDWR | O_DIRECT);
	assert(fd);

	uint64_t num_ops = 50 * num_blks;
	float cp = num_blks * (cache_perc / 100.0);
	uint64_t cache_size = (uint64_t)cp;
	auto cache = cache_size != 0 ? new fifo_cache_t<uint64_t, std::string>(cache_size) : NULL;
	std::chrono::time_point<std::chrono::high_resolution_clock> start_capacity;
	std::unordered_set<uint64_t> accessed_blocks;

	long long elapsed_capacity = 0;
	uint64_t miss_capacity = 0;
	uint64_t non_cold_access = 0;
	bool timed_capacity = false;

	uint64_t i = 0;
	uint64_t cache_misses = 0;
	static char buf[BLKSZ] __attribute__((__aligned__(BLKSZ)));

	std::default_random_engine generator;
	generator.seed(0);
	zipfian_int_distribution<int> zipf(0, num_blks - 1, 0.5);

	auto zipf_rand = [&]() { return zipf(generator); };
	srand(0);

	auto start = std::chrono::high_resolution_clock::now();
	while (i++ < num_ops) {
		uint64_t blk_read = zipf_rand() % num_blks;
		if (accessed_blocks.find(blk_read) == accessed_blocks.end()) {
			accessed_blocks.insert(blk_read);
		} else {
			// non-cold access
			start_capacity = std::chrono::high_resolution_clock::now();
			timed_capacity = true;
			non_cold_access++;
		}
		if (cache && cache->Cached(blk_read)) {
			auto ret = cache->Get(blk_read);
		} else {
			assert(pread(fd, buf, BLKSZ, blk_read * BLKSZ) == BLKSZ);
			std::string s(buf);
			// std::cout << "blk_read: " << blk_read << ", string: " << s << std::endl;
			assert(stoi(s) == blk_read);
			if (cache) {
				std::string contents(buf, BLKSZ);
				cache->Put(blk_read, contents);
			}
			cache_misses++;
			if (timed_capacity)
				miss_capacity++;
		}
		if (timed_capacity) {
			elapsed_capacity += std::chrono::duration_cast<std::chrono::microseconds>(
						    std::chrono::high_resolution_clock::now() - start_capacity)
						    .count();
			timed_capacity = false;
		}
	}

	auto elapsed = std::chrono::high_resolution_clock::now() - start;

	long long total_time = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

	std::cout << "run statistics: " << std::endl
		  << "\tcache percentage: " << cache_perc << std::endl
		  << "\ttotal time taken in us: " << total_time << std::endl
		  << "\ttotal misses: " << cache_misses << std::endl
		  << "\tcapacity misses: " << miss_capacity << std::endl
		  << "\tnon-cold access: " << non_cold_access << std::endl
		  << "\tnon-cold time total in us: " << elapsed_capacity << std::endl;

	return EXIT_SUCCESS;
}
