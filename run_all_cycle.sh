#!/bin/bash

echo "run ./aiger_cec"

for i in {4..66}
do
  echo "cec_cycle$i.aig run"
  ./aiger_cec ./aig_test/cec_cycle$i.aig ./new_aig/new$i.aig > ./log/cycle$i.log
done

echo "finished"
