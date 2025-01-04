#!/bin/bash

echo "run ./cec_ocond"

for i in {4..66}
do
  echo "cec_cycle$i.aig run"
  ./condec_test ./aig_test/cec_cycle$i.aig > ./log/cycle$i.log
done

echo "finished"
