#include <iostream>
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

int main(int argc, char** argv) {
	char* buf;
	int numblks = atoi(argv[1]);
	int fd = open("/dev/sdb", O_RDWR | O_CREAT, S_IRWXU);
	assert(fd);
	buf = (char*) malloc(numblks * 4096);
	int i;
	for (i = 0; i < numblks; i++)
	{
		sprintf(&buf[i * 4096], "%d", i);
	}
	
	auto start = std::chrono::high_resolution_clock::now();
	write(fd, buf, numblks * 4096);
	fsync(fd);
	auto elapsed = std::chrono::high_resolution_clock::now() - start;

	long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
	std::cout << "Writing "<< numblks << " blks took " << microseconds/1000.0 << " ms" << std::endl;
}
