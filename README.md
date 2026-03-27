<div align="center">

<h1>[ISEDA'26] CondEC: Equivalence Checking under Conditions</h1>

<h5 align="center"> If you find this project useful, please give us a star🌟.


<h5 align="center"> 

<a href="https://github.com/WBChe/CondEC"><img src="https://img.shields.io/badge/Paper-Link-red"></a>
<a href="https://figshare.com/s/0d69d6d95527ecccfaa4"><img src="https://img.shields.io/badge/Result-Figshare-yellow">


[Wenbin Che](https://github.com/WBChe/CondEC),
[Changyuan Yu](),
[Hongce Zhang]()<sup>✉️</sup>


[The Hong Kong University of Science and Technology (Guangzhou)](https://www.hkust-gz.edu.cn/)

<sup>✉️</sup>Corresponding Author

</h5>
</div>


## Description
**CondEC** is a constraint-aware framework for **conditional equivalence checking**, where functional equivalence is required only under user-specified conditions rather than over the entire input space.
Unlike conventional combinational equivalence checking, which assumes full input-space equivalence and treats constraints as auxiliary Boolean logic, CondEC explicitly incorporates functional constraints into the equivalence checking workflow. This enables efficient verification of designs whose equivalence holds only in semantically meaningful operating modes.

## Setup
Dependencies:
- C++17 compiler (e.g., `g++`)
- CaDiCaL (fixed commit)
- AIGER library sources (included: `aiger.c/.h`)

Install CondEC and CaDiCaL from the project root:

```bash
git clone https://github.com/WBChe/CondEC
cd ./CondEC
git clone https://github.com/arminbiere/cadical.git
cd cadical
git checkout 7b99c07f0bcab5824a5a3ce62c7066554017f641
./configure && make
cd ..
```

## Quickstart
Build:

```bash
make
```

Run:

```bash
./condec <aigfile> [-option]
```

Common options:
- `-h` print help
- `-q` quiet mode (only prints final result and time)

Example:

```bash
./condec benchmarks/aig/aa1_cond.aig
```

## Experiment
CondEC has been evaluated on a diverse set of industrial-style and competition benchmarks, including constrained datapath and arithmetic-intensive designs.
Batch evaluation uses `test.sh` and processes all `benchmarks/aig/*.aig` files. It writes a CSV summary to `results.csv`.

```bash
./test.sh
```


For SAT solver (ensure the solver is properly installed and the paths in the .sh scripts are correctly configured):

```bash
./test_kissat.sh # for kissat test

./test_cadical.sh # for cadical test
```

For ABC (ensure ABC is installed):

```bash
abc -c "&r benchmarks/aig-and-output/*.aig; &cec -m;" # for one test

./test_abc-cec.sh # for all test
```


<!-- ## Citation
If you find this repository is useful, please star🌟 this repo and cite🖇️ our paper.

```bibtex
WIP
``` -->

## Acknowledgment
Our work is primarily based on the following codebases. We are sincerely grateful for their work.
- [CaDiCaL](https://github.com/arminbiere/cadical) SAT solver
- [AIGER](https://github.com/arminbiere/aiger) circuit format and parser
