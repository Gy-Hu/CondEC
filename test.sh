#!/bin/bash


find ./test -type f | while read -r file; do
#find ./aig_test -type f | while read -r file; do
    #echo "test file name: $file"
    
    start_time=$(date +%s)

    timeout 3600 ./condec_test $file -q
    exit_code=$?

    end_time=$(date +%s)
    duration=$((end_time - start_time))

    if [ $exit_code -eq 124 ]; then
        echo "$file, timeout"
    fi
done





