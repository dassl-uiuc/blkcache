#include <iostream>
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include "lru.h"

#define BLKSZ 4096

int main(int argc, char** argv) {
	int fd = open("/dev/mapper/disag_blk_target_device", O_RDWR | O_DIRECT);
	assert(fd);
	int num_blks = atoi(argv[1]);
	uint64_t num_ops = 10 * num_blks;	
	auto start = std::chrono::high_resolution_clock::now();

	uint64_t i = 0;
	static char buf[BLKSZ] __attribute__ ((__aligned__ (BLKSZ)));
	srand(time(NULL));
	while(i++ < num_ops)
	{
		uint64_t blk_read = rand()%num_blks;
		assert(pread(fd, buf, BLKSZ, blk_read * BLKSZ) == BLKSZ);
		if (i % 10000 == 0) printf("ops_finished: %lu\n", i);
	}

	auto elapsed = std::chrono::high_resolution_clock::now() - start;

	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	std::cout << "Reading " << num_ops << " random blocks took " << microseconds << " us" << std::endl;
	
}
