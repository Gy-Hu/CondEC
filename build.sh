#!/bin/bash

echo "build start"

g++ -g -c condec.cpp -o condec.o
echo "build condec.o"
g++ -g condec_test.cpp aiger/aiger.o kissat_extras/build/libkissat.a condec.o -o condec_test
echo "build condec_test"

echo "build finished"

#./aiger_cec ./aig_test/cec_cycle4.aig
#./aiger_cec_precond ./alu_test/alu_miter.aig
