#include <iostream>
#include <vector>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <random>
#include <algorithm>

#include "zipfian_distribution.h"

int main() {
    std::default_random_engine generator;
    generator.seed(0);
    zipfian_int_distribution<int> zipf(0, 10000, 0.5);

    auto zipf_rand = [&]() { return zipf(generator); };

    int *frequencies = new int[10000];
    for (int i = 0; i < 10000; i++) frequencies[i] = 0;
    for (int i = 0; i < 10 * 10000; i++)
    {
        frequencies[zipf_rand()]++;
    }

    std::sort(frequencies, frequencies + 10000, std::greater<int>());
    for (int i = 0; i < 10000; i++)
    {
        std::cout << frequencies[i] << std::endl;
    }
    return 0;
}