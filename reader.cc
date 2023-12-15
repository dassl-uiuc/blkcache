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
	int fd = open("/rbd/foo", O_RDWR | O_DIRECT);
	assert(fd);
	int cache_perc = atoi(argv[1]);
	int num_blks = atoi(argv[2]);

	uint64_t num_ops = 10 * num_blks;	
	float cp = num_blks * (cache_perc/100.0);
	uint64_t cache_size = (uint64_t) cp;
	std::cout << "Cache size:"<<  cache_perc <<"%; Absolute size:" << cache_size << std::endl;
	auto cache = new fifo_cache_t<uint64_t, std::string>(cache_size);
	auto start = std::chrono::high_resolution_clock::now();

	uint64_t i = 0;
	uint64_t cache_misses = 0;
	static char buf[BLKSZ] __attribute__ ((__aligned__ (BLKSZ)));
	srand(0);
	while(i++ < num_ops)
	{
		uint64_t blk_read = rand()%num_blks;
		if(cache->Cached(blk_read)) {
			auto ret = cache->Get(blk_read);		
		} else {
			//std::cout << "Reading..."<<blk_read<<std::endl;
			assert(pread(fd, buf, BLKSZ, blk_read * BLKSZ) == BLKSZ);
			std::string contents(buf, BLKSZ);
			cache->Put(blk_read, contents);
			cache_misses++;
		}
	}

	auto elapsed = std::chrono::high_resolution_clock::now() - start;

	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        elapsed).count();
	std::cout << cache_perc <<"%\t"<< microseconds << "\t" << cache_misses<< std::endl;
	
}
