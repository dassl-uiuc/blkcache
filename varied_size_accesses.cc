#include <iostream>
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <random>
#include <assert.h>

#define BLKSZ (uint64_t)4096
#define SECTORSZ (uint64_t)512

int main(int argc, char **argv)
{
	int num_blks = atoi(argv[1]);
	std::string file_name(argv[2]);
	int fd = open(file_name.c_str(), O_RDWR | O_DIRECT);
	assert(fd);

	uint64_t total_access_size = 50 * num_blks * BLKSZ;
	static char buf[16 * SECTORSZ] __attribute__((__aligned__(SECTORSZ)));

	srand(0);
	auto start_time = std::chrono::high_resolution_clock::now();
	while (total_access_size > 0) {
		uint64_t access_size = std::min((rand() % 10 + 1) * SECTORSZ, total_access_size);
		uint64_t start_offset = rand() % (num_blks * 8 - (access_size / SECTORSZ));
		assert(pread(fd, buf, access_size, start_offset * SECTORSZ) == access_size);
		for (int i = 0; i < access_size; i += SECTORSZ) {
			char s[SECTORSZ];
			memcpy(s, &buf[i], SECTORSZ);
			std::string str(s);
			assert(std::stoi(str) == i / SECTORSZ + start_offset);
		}
		total_access_size -= access_size;
	}
	auto end_time = std::chrono::high_resolution_clock::now();

	std::cout << "run statistics: " << std::endl
		  << "\ttotal time taken: "
		  << std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() << " us"
		  << std::endl;
	return EXIT_SUCCESS;
}
