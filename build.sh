#!/bin/bash

echo "build start"

g++ -g -c condec.cpp -o condec.o
g++ -g aiger_cec.cpp aiger/aiger.o cadical/build/libcadical.a condec.o -o aiger_cec

echo "build finished"

echo "run ./aiger_cec"
./aiger_cec ./aig_test/cec_cycle4.aig
#./aiger_cec cec_cycle10.aig
