#!/bin/bash

OUT=results_abc_cec.csv
TIMEOUT=3600

echo "filename,result,time_sec" > $OUT

for f in benchmarks/aig-and-output/*.aig; do
  base=$(basename "$f")

  output=$(timeout $TIMEOUT abc -c "&r $f; &cec -m; quit" 2>&1)
  status=$?

  if [ $status -eq 124 ]; then
    # timeout
    result="TIMEOUT"
    time="$TIMEOUT"
  else
    if echo "$output" | grep -q "Networks are equivalent"; then
      result="UNSAT"
    elif echo "$output" | grep -q "Networks are NOT EQUIVALENT"; then
      result="SAT"
    else
      result="UNKNOWN"
    fi

    time=$(echo "$output" | grep "Time =" | sed -E 's/.*Time = *([0-9.]+).*/\1/')
  fi

  echo "$base,$result,$time" >> $OUT
done
