#!/bin/bash

# Iterate over cache percentages in the range of 10 to 100 with a step of 10
for cache_percentage in {0..100..10}
do
    echo "Running for $cache_percentage"
    sudo ./writer 10000 /dev/sdb
    sudo ./reader $cache_percentage 10000 /dev/sdb
done