#!/bin/bash

echo "build start"

g++ -g -c condec.cpp -o condec.o
g++ -g cec_ocond.cpp aiger/aiger.o kissat_extras/build/libkissat.a condec.o -o cec_ocond
g++ -g cec_icond.cpp aiger/aiger.o kissat_extras/build/libkissat.a condec.o -o cec_icond

echo "build finished"

#./aiger_cec ./aig_test/cec_cycle4.aig
#./aiger_cec_precond ./alu_test/alu_miter.aig
