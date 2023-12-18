#include <iostream>
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include "cache.hpp"
#include "fifo_cache_policy.hpp"

#define BLKSZ 4096

template <typename Key, typename Value>
using fifo_cache_t = typename caches::fixed_sized_cache<Key, Value, caches::FIFOCachePolicy>;

int main(int argc, char** argv) {
	int cache_perc = atoi(argv[1]);
	int num_blks = atoi(argv[2]);
	std::string file_name(argv[3]);
	int fd = open(file_name.c_str(), O_RDWR | O_DIRECT);
	assert(fd);

	uint64_t num_ops = 10 * num_blks;	
	float cp = num_blks * (cache_perc/100.0);
	uint64_t cache_size = (uint64_t) cp;
	std::cout << "Cache size:"<<  cache_perc <<"%; Absolute size:" << cache_size << std::endl;
	auto cache = cache_size != 0 ? new fifo_cache_t<uint64_t, std::string>(cache_size): NULL;
	auto start = std::chrono::high_resolution_clock::now();
	std::chrono::time_point<std::chrono::high_resolution_clock> start_capacity;
	std::unordered_set<uint64_t> accessed_blocks;

	long long elapsed_capacity = 0;
	uint64_t miss_capacity = 0;
	uint64_t non_cold_access = 0;
	bool timed_capacity = false;

	uint64_t i = 0;
	uint64_t cache_misses = 0;
	static char buf[BLKSZ] __attribute__ ((__aligned__ (BLKSZ)));
	srand(0);
	while(i++ < num_ops)
	{
		uint64_t blk_read = rand()%num_blks;
		if (accessed_blocks.find(blk_read) == accessed_blocks.end()) {
			accessed_blocks.insert(blk_read);
		} else {
			// non-cold access
			start_capacity = std::chrono::high_resolution_clock::now();
			timed_capacity = true;
			non_cold_access++;
		}
		if(cache && cache->Cached(blk_read)) {
			auto ret = cache->Get(blk_read);		
		} else {
			assert(pread(fd, buf, BLKSZ, blk_read * BLKSZ) == BLKSZ);
			if (cache)
			{
				std::string contents(buf, BLKSZ);
				cache->Put(blk_read, contents);
			}
			cache_misses++;
			if (timed_capacity)
				miss_capacity++;
		}
		if (timed_capacity) {
			elapsed_capacity += std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::high_resolution_clock::now() - start_capacity).count();
			timed_capacity = false;
		}
	}

	auto elapsed = std::chrono::high_resolution_clock::now() - start;

	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        elapsed).count();
	std::cout << cache_perc <<"%\t"<< microseconds << "\t" << cache_misses<< std::endl;
	std::cout << "capacity misses: " << miss_capacity << "\tnon-cold access: " << non_cold_access 
		<< "\tnon-cold time total: " << elapsed_capacity << std::endl;
	
}
