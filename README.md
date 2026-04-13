<div align="center">

<h1> CondEC: Equivalence Checking under Conditions</h1>

<h5 align="center"> If you find this project useful, please give us a star🌟.


<h5 align="center"> 

<a href="https://github.com/WBChe/CondEC"><img src="https://img.shields.io/badge/Paper-Link-red"></a>
<a href='https://huggingface.co/collections/wbche/condec'><img src='https://img.shields.io/badge/%F0%9F%A4%97%20Hugging%20Face-Dataset-blue'>
<a href="https://figshare.com/s/0d69d6d95527ecccfaa4"><img src="https://img.shields.io/badge/Result-Figshare-yellow">


[Wenbin Che](),
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
- `git`, `make`, `python3`, `timeout`, `/usr/bin/time`
- Slurm `sbatch` command for the comparison pipeline
- AIGER library sources (included: `aiger.c/.h`)

One-time local benchmark setup uses these fixed paths:
- `tools/abc/abc`
- `tools/kissat/build/kissat`
- `cadical/build/cadical`
- `benchmark_artifacts/condec_baseline`
- `benchmark_artifacts/condec_improved`

If any of them are missing, build or place them there once. The benchmark
script also accepts manual overrides through `ABC_BIN`, `KISSAT_BIN`,
`CADICAL_SOLVER_BIN`, `BASELINE_CONDEC_BIN`, and `IMPROVED_CONDEC_BIN`.

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
For a full baseline-vs-current Slurm comparison benchmark, make sure the paths
above already exist and then submit:

```bash
bash benchmark_compare.slurm.sh
```

By default the benchmark submission script prefers project-local tools and prepared artifacts at:
- `tools/abc/abc`
- `tools/kissat/build/kissat`
- `cadical/build/cadical`
- `benchmark_artifacts/condec_baseline`
- `benchmark_artifacts/condec_improved`

You can still override them explicitly with environment variables such as `ABC_BIN`, `KISSAT_BIN`, `CADICAL_SOLVER_BIN`, `BASELINE_CONDEC_BIN`, and `IMPROVED_CONDEC_BIN`.

The Slurm submission also uses explicit runtime parameters instead of leaving
array concurrency up to the scheduler defaults. Current defaults are:
- `PARTITION=q-lxe5wipa`
- `MAX_PARALLEL=59` (matches benchmark count; cluster has 576 idle CPUs across hk01dgx042/043/055, so all 59 fit easily)
- `WORKER_CPUS=1`
- `WORKER_MEM=8G`
- `TIMEOUT_SEC=3600`
- `WORKER_TIME=05:30:00` by default from `TIMEOUT_SEC * 5 + 1800`
- `COLLECT_CPUS=1`
- `COLLECT_MEM=4G`
- `COLLECT_TIME=00:10:00`

For CondEC-only batch evaluation on `benchmarks/aig/*.aig`:

```bash
./test.sh
```

For standalone solver checks with project-local paths:

```bash
./test_kissat.sh
./test_cadical.sh
./test_abc-cec.sh
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
