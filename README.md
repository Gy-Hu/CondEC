# CondEC
Conditional Equivalence Checking

### How to start

    git clone https://github.com/arminbiere/aiger
    cd aiger
    ./configure.sh && make
    cd ..

    git clone https://github.com/arminbiere/cadical.git
    cd cadical
    ./configure && make
    cd ..

### build

    ./build.sh

### run i-condec 
For we want have condition in inputs

e.g. module mul(input [4:0] a, input [4:0] b, input [3:0] control, output [9:0] out); CEC condition: control == 4'b1000

    ./run_icond.sh

### run o-condec
For we want have condition in outputs

e.g. module mul(input [4:0] a, input [4:0] b, output valid, output [9:0] out); CEC condition: valid == 1'b1

    ./run_ocond.sh
