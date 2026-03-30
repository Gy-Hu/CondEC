# TODO: Untried Optimization Opportunities

This document tracks optimization ideas that were identified during analysis but not yet implemented, due to scope, risk, or architectural complexity.

---

## Data Structure Overhaul

### Replace `unordered_map` with `vector` for sequential integer keys
- **Files:** `node_condec.h:76-91`
- **Problem:** Node IDs are sequential integers starting from 1 (`create_new_node()` increments), but stored in `std::unordered_map<unsigned, ...>`. This uses a hash table to implement what should be an array — O(1) amortized but with terrible cache locality and constant-factor overhead.
- **Targets:**
  - `lit_node_map` → `std::vector<node_neg>` indexed by `aiger_strip(lit)`
  - `node_lit_map` → `std::vector<unsigned>` indexed by node ID
  - `node_satvar_map` → `std::vector<int>` indexed by node ID
  - `node_simulation_hash_map` → `std::vector<sim_hash_t>` indexed by node ID
  - `node_simulation_data_map` → `std::vector<std::vector<inputs_t>>` indexed by node ID
  - `node_cond_data_map` → `std::vector<std::vector<inputs_t>>` indexed by node ID
- **Keep as unordered_map:** `structural_hash_nodevec_map` and `simulation_hash_nodevec_map` (non-sequential hash keys)
- **Estimated gain:** 3-10x speedup on map lookups (the dominant operation in hot loops)
- **Effort:** Medium — need to pre-allocate to `model->maxvar` size, add bounds checking

---

## SAT Solver Clause Management

### Periodic solver reconstruction to clear dead clauses
- **Files:** `node_condec.cpp:234-280` (`cec_checker`)
- **Problem:** Every `cec_checker` call creates 2 new SAT variables and 7 new clauses (AND gate + XOR miter), then "disables" them via `unit(assumption)`. These dead clauses permanently accumulate in the solver, causing:
  - Growing memory usage
  - Slower BCP (Boolean Constraint Propagation)
  - Degraded performance on large circuits (100k+ gates)
- **Solution:** Periodically (e.g., every 1000 gates) create a fresh `CaDiCaL::Solver`, re-add all confirmed equivalences and circuit structure as permanent clauses
- **Estimated gain:** Significant for large circuits where dead clause ratio exceeds 50%
- **Effort:** Medium — need to track which clauses are permanent vs temporary

### Upgrade CaDiCaL to development branch for `push()`/`pop()` API
- **Branch:** `origin/development` (472 commits ahead of master/v3.0.0)
- **Problem:** Current assumption-based clause disabling is a workaround. The development branch adds `push()`/`pop()` context stack, allowing clean scoped clause management:
  ```cpp
  solver_->push();
  create_andgate(...);
  create_miter(...);
  solver_->assume(miter_o);
  int res = solver_->solve();
  solver_->pop();  // all temporary clauses automatically removed
  ```
- **Additional benefits from development branch:**
  - `86330f3e`: "avoid quadratic behavior in incremental calls" — directly fixes repeated SAT call performance
  - `0c7af5bd`: option to unbump clauses in incremental calls
  - `fb592858`: `ppassumptions` option for assumption ordering
  - New tuning options: `vivifyinst`, `luckyrandom`, `walkddfwstrat`, `varindexorder`
- **Risk:** 472-commit gap may introduce API breaking changes; needs thorough compatibility testing
- **Effort:** High — need to test all benchmarks for correctness after upgrade

---

## SAT Solver Tuning

### Dynamic conflict limit based on circuit depth
- **Files:** `node_condec.cpp:250, 300`
- **Problem:** All SAT calls use a fixed conflict limit of 10,000. But:
  - Bottom-level gates (small cone of influence): 100 conflicts would suffice
  - Top-level gates (large cone): 10,000 may not be enough
- **Solution:** Scale conflict limit with the gate's depth or cone size:
  ```cpp
  int conflict_limit = std::min(1000 + depth * 200, 50000);
  solver_->limit("conflicts", conflict_limit);
  ```
- **Estimated gain:** Faster for easy gates (majority), more thorough for hard gates
- **Effort:** Low — but need a way to compute gate depth (topological order is already available from AIGER)

### Leverage CaDiCaL's built-in SAT sweeping and congruence closure
- **Problem:** CondEC manually implements simulation + SAT-based equivalence checking, which is essentially SAT sweeping. CaDiCaL v3.0.0 has built-in:
  - `sweep=1` (SAT sweeping, default on)
  - `congruence=1` (congruence closure with AND/XOR/ITE gate extraction)
- **Idea:** Encode the entire circuit + constraints as CNF, then let CaDiCaL's internal sweep/congruence do the equivalence merging, rather than driving it externally
- **Caveat:** This would bypass CondEC's constraint-aware simulation (the paper's core contribution). More of an academic comparison point than a practical optimization
- **Effort:** High (architectural rewrite)

---

## Simulation Engine

### SIMD-accelerated simulation (AVX2/AVX-512)
- **Files:** `node_condec.cpp:556-563` (sim data loop), `generate_initial_sim_hash_data`
- **Problem:** Current simulation uses `uint64_t` bitwise AND — 64 inputs simulated in parallel. With AVX2 `__m256i`, we could simulate 256 inputs per operation (4x throughput)
- **Prerequisite:** Requires restructuring simulation data from AoS (Array of Structures, current `unordered_map<node, vector<uint64_t>>`) to SoA (Structure of Arrays) layout for SIMD-friendly memory access
- **Estimated gain:** 4x simulation throughput (simulation is a small fraction of total time though — SAT dominates)
- **Effort:** High — data layout restructuring touches most of the codebase

### Increase simulation rounds dynamically
- **Files:** `node_condec.h:17-18`
- **Problem:** `INITIAL_SIM_ROUND = 10` is hardcoded. For large circuits, 640 bits of simulation data may not provide enough discrimination, leading to false equivalence candidates that waste SAT calls
- **Solution:** Start with 10 rounds, monitor the false-positive rate (SAT calls that return SAT instead of UNSAT), and dynamically add more simulation rounds when the rate is high
- **Effort:** Medium

---

## Constraint Handling

### Correct De Morgan decomposition for negated AND constraints
- **Files:** `main.cpp:68-120`
- **Problem (known bug in original code):** When a constraint is `NOT(AND(a,b))`, the code decomposes it as `AND(NOT a, NOT b)` (one-level De Morgan). But `NOT(AND(a,b)) = OR(NOT a, NOT b)`, not `AND(NOT a, NOT b)`. The current decomposition creates a **strictly stronger** condition than intended
- **Impact:** The equivalence check is done under a tighter constraint than the user specified. Results that are UNSAT (equivalent) are still correct (if equivalent under stronger condition, also equivalent under weaker). But SAT results could be wrong — two circuits that differ under `OR(NOT a, NOT b)` might appear equivalent under `AND(NOT a, NOT b)`
- **Note:** We attempted recursive De Morgan but it amplified this incorrectness. A proper fix requires encoding OR constraints (e.g., via Tseytin transformation) rather than decomposing into AND leaves
- **Effort:** Medium — need to introduce auxiliary variables for OR decomposition

---

## Code Quality

### Fix the annotated neg-merge bug
- **Files:** `node_condec.cpp:641` (developer's own comment: `// bug need to modify  lit -> -node`)
- **Problem:** During negative equivalence merge, the `lit_node_map` mapping may be inconsistent in edge cases. The original developer flagged this but did not fix it
- **Effort:** Low-Medium — need to trace through all consumers of `lit_node_map` after neg merge

### Multi-output support
- **Files:** `main.cpp:63`
- **Problem:** Only `outputs[0]` is checked. Multi-output circuits require iterating over all outputs
- **Effort:** Medium

### Latch (sequential circuit) support
- **Problem:** `model->num_latches` is printed but never handled. Sequential circuits would need unrolling or k-induction
- **Effort:** High
