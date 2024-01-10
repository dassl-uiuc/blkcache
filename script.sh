#!/bin/bash

cache_percentages=("0" "10" "20" "30" "40" "50" "60" "70" "80" "90" "100") 
# Iterate over cache percentages in the range of 10 to 100 with a step of 10
for cache_percentage in "${cache_percentages[@]}"
do
    echo "Running for $cache_percentage"
    sudo ./writer 10000 /dev/sdb
    echo 3 > /proc/sys/vm/drop_caches
    sudo ./reader $cache_percentage 10000 /dev/sdb
done
