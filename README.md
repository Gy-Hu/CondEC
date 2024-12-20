# CondEC
Conditional Equivalence Checking

### How to start

    git clone https://github.com/arminbiere/aiger.git
    cd aiger
    ./configure.sh && make
    cd ..

    git clone https://github.com/jix/kissat_extras.git
    cd kissat_extras
    ./configure && make
    cd ..

### Build

    ./build.sh

### Run condec
For we want have condition in outputs

e.g. module mul(input [4:0] a, input [4:0] b, output valid, output [9:0] out); CEC condition: valid == 1'b1, refer to `/aig_test`

e.g. module mul(input [4:0] a, input [4:0] b, input [3:0] control, output [9:0] out); CEC condition: control == 4'b1000, refer to `/alu_test`

    ./run.sh
