#pragma once


#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <cmath>
#include "cadical/src/cadical.hpp"

extern "C" {
#include "aiger.h"
}

typedef uint64_t inputs_t;
typedef uint64_t sim_hash_t;

#define INITIAL_ROUND 15    // need to more than INITIAL_SIM_ROUND, because not every bit can sat condition
#define INITIAL_SIM_ROUND 10    // sim data

// Contextual bandit for adaptive SAT conflict limit selection.
// Uses a simplified LinUCB (disjoint linear model per arm) with
// a 4-dimensional context: [gate_position, num_candidates, recent_merge_rate, 1(bias)].
class ConflictBandit {
    static constexpr int NUM_ARMS = 5;
    static constexpr int CTX_DIM = 4;
    static constexpr double ALPHA = 0.15;  // exploration parameter (conservative)

    int arms[NUM_ARMS] = {500, 2000, 5000, 10000, 50000};

    // Per-arm linear model: A_inv (CTX_DIM x CTX_DIM) and b (CTX_DIM)
    // Using simplified diagonal approximation for efficiency
    double A_diag[NUM_ARMS][CTX_DIM];  // diagonal of A inverse
    double b_vec[NUM_ARMS][CTX_DIM];   // reward-weighted context sum
    int arm_counts[NUM_ARMS];

public:
    ConflictBandit() {
        for (int a = 0; a < NUM_ARMS; a++) {
            arm_counts[a] = 0;
            for (int d = 0; d < CTX_DIM; d++) {
                A_diag[a][d] = 1.0;  // identity initialization
                b_vec[a][d] = 0.0;
            }
        }
    }

    int consecutive_unknown = 0;
    // warmup phase: use fixed limit and collect stats to decide if bandit is suitable
    static constexpr int WARMUP_CALLS = 64;
    int warmup_count = 0;
    int warmup_unsat = 0;
    bool bandit_enabled = false;  // disabled until warmup proves it's suitable
    static constexpr double BANDIT_ENABLE_THRESHOLD = 0.6;  // need >60% UNSAT rate

    void warmup_update(bool is_unsat) {
        if (bandit_enabled) return;
        warmup_count++;
        if (is_unsat) warmup_unsat++;
        if (warmup_count >= WARMUP_CALLS) {
            double unsat_rate = (double)warmup_unsat / warmup_count;
            bandit_enabled = (unsat_rate >= BANDIT_ENABLE_THRESHOLD);
        }
    }

    int select_arm(double gate_pos, int num_candidates, double merge_rate) {
        // during warmup or if bandit disabled: use default
        if (!bandit_enabled) return 10000;

        // safety fallback: if too many consecutive UNKNOWNs, use high limit
        if (consecutive_unknown >= 3) {
            return arms[NUM_ARMS - 1];
        }

        double ctx[CTX_DIM] = {gate_pos, (double)num_candidates, merge_rate, 1.0};

        int best_arm = 3;  // default: 10000
        double best_ucb = -1e18;

        for (int a = 0; a < NUM_ARMS; a++) {
            double pred = 0, uncertainty = 0;
            for (int d = 0; d < CTX_DIM; d++) {
                double theta_d = A_diag[a][d] * b_vec[a][d];
                pred += theta_d * ctx[d];
                uncertainty += A_diag[a][d] * ctx[d] * ctx[d];
            }
            double ucb = pred + ALPHA * std::sqrt(uncertainty);

            if (ucb > best_ucb) {
                best_ucb = ucb;
                best_arm = a;
            }
        }
        return arms[best_arm];
    }

    void update(int conflict_limit, double gate_pos, int num_candidates,
                double merge_rate, double reward, bool was_unknown = false) {
        if (was_unknown) consecutive_unknown++;
        else consecutive_unknown = 0;
        // find arm index
        int a = 3;
        for (int i = 0; i < NUM_ARMS; i++) {
            if (arms[i] == conflict_limit) { a = i; break; }
        }
        double ctx[CTX_DIM] = {gate_pos, (double)num_candidates, merge_rate, 1.0};

        arm_counts[a]++;
        for (int d = 0; d < CTX_DIM; d++) {
            A_diag[a][d] = 1.0 / (1.0 / A_diag[a][d] + ctx[d] * ctx[d]);
            b_vec[a][d] += reward * ctx[d];
        }
    }
};

class CondEC
{
private:
    /* data */
    aiger * model_;
    CaDiCaL::Solver *solver_;
    unsigned node_number;   // node is our new model node
    unsigned structural_hash_merge_num;
    unsigned cec_merge_num;

    unsigned cec_sat_num;
    unsigned cec_unknow_num;
    unsigned cec_unsat_num;

    std::vector<std::vector<bool>> initial_pattern_vec;

    std::vector<std::vector<bool>> new_input_pattern_vec;   // for storage new sim data
    int new_sim_data_num;   // new sim data number

    unsigned create_new_node(){ return ++node_number;}

    // solver
    int solver_satvar;
    int create_satvar(){ 
        auto available_var = solver_ -> declare_one_more_variable();   // Returns the next fresh variable that was not used internally.
        return available_var;
    }
    int condition_satvar;

public:
    /*  map:
        lit             -> from aiger model
        node            -> our new model create
        satvar          -> picosat create
        structural_hash -> CondEC create
        simulation_hash -> CondEC create
        simulation_data -> CondEC create
    */

    /*  new map:
        lit             -> from aiger model
        node            -> new struct create
        satvar          -> sat solver create
        structural_hash -> CondEC create
        simulation_hash -> CondEC create
        simulation_data -> CondEC create

        lit -> {node, neg} -> satvar
    */

    struct node_neg{
        unsigned node;
        unsigned neg;
    };

    static constexpr unsigned INVALID_NODE = 0;  // node 0 is unused (nodes start from 1)

    // lit <-> node  (indexed by aiger_strip(lit), pre-allocated to maxvar*2+2)
    std::vector<node_neg>                lit_node_map;
    // node -> origin lit (indexed by node id)
    std::vector<unsigned>                node_lit_map;

    // node -> satvar (indexed by node id)
    std::vector<int>                     node_satvar_map;

    // node <-> structural hash (hash keys are non-sequential, keep as unordered_map)
    std::unordered_map<int, std::vector<unsigned>>        structural_hash_nodevec_map;

    // node -> sim hash (indexed by node id)
    std::vector<sim_hash_t>              node_simulation_hash_map;
    // sim_hash -> node vec (hash keys are non-sequential, keep as unordered_map)
    std::unordered_map<sim_hash_t, std::vector<unsigned>>   simulation_hash_nodevec_map;

    // node -> sim data vec (indexed by node id)
    std::vector<std::vector<inputs_t>>   node_simulation_data_map;
    std::vector<std::vector<inputs_t>>   node_cond_data_map;

    // check if lit has been mapped to a node
    bool lit_has_node(unsigned lit) const {
        unsigned idx = aiger_strip(lit);
        return idx < lit_node_map.size() && lit_node_map[idx].node != INVALID_NODE;
    }


    // generate 64 bit random data for initial sim hash and sim data
    inputs_t get_random_uint64();

    // get simulation hash of node
    sim_hash_t get_simulation_hash(unsigned lit);

    // get simulation data of node
    inputs_t get_simulation_data(unsigned lit, int sim_round);

    // new create node number
    unsigned get_node_num(){return node_number;}

    // get the satvar from lit, lit -> node -> satvar
    int get_satvar(unsigned int lit);


    /*************************** CondEC MAIN FUNCTION ******************************/
    // create one node hash key
    int create_structural_hash(unsigned rsh0, unsigned rsh1);

    // for inputs, generate initial 1 sim hash and INITIAL_SIM_ROUND sim pattern that sat condition
    bool generate_initial_sim_hash_data(unsigned condition_lit, std::vector<int> condition_vec);

    // bandit for adaptive conflict limit
    ConflictBandit conflict_bandit;
    double bandit_gate_pos;        // current gate position [0, 1]
    double bandit_merge_rate;      // recent merge success rate
    unsigned bandit_recent_merge;  // recent merge count
    unsigned bandit_recent_total;  // recent total SAT calls

    // cec and-gates stage
    bool cec_checker(const std::vector<unsigned> &cec_candidate, unsigned &equivalence_node, int rhs0_satvar, int rhs1_satvar, int lhs_satvar);
    bool cec_checker_neg(const std::vector<unsigned> &cec_candidate, unsigned &equivalence_node, int rhs0_satvar, int rhs1_satvar, int lhs_satvar);

    // create new input sim data
    void create_new_simulation_data();

    // update new sim data after create_new_simulation_data()
    void update_all_sim_data(unsigned int lhs_lit_end);


    /********************************* API *****************************************/
    // cec inputs stage
    void cec_inputs_register();

    // cec conditional outputs stage
    void cec_condition_register(unsigned int &condition_output, std::vector<int> &condition_vec);

    // cec and-gates stage
    void cec_ands_register();

    // cec final solve stage
    int cec_solve();

    CondEC(aiger *model, CaDiCaL::Solver *solver)
        : model_(model), solver_(solver), node_number(0),
          structural_hash_merge_num(0), cec_merge_num(0),
          cec_sat_num(0), cec_unknow_num(0), cec_unsat_num(0),
          initial_pattern_vec(0), new_input_pattern_vec(0),
          new_sim_data_num(0), solver_satvar(0),
          bandit_gate_pos(0), bandit_merge_rate(0.5),
          bandit_recent_merge(0), bandit_recent_total(0)
    {
        // Pre-allocate vectors indexed by lit/node for O(1) access
        unsigned max_lit = model->maxvar * 2 + 2;
        unsigned max_nodes = model->maxvar + model->num_inputs + 2;
        lit_node_map.resize(max_lit, {INVALID_NODE, 0});
        node_lit_map.resize(max_nodes, 0);
        node_satvar_map.resize(max_nodes, 0);
        node_simulation_hash_map.resize(max_nodes, 0);
        node_simulation_data_map.resize(max_nodes);
        node_cond_data_map.resize(max_nodes);
    }


    /********************************* Cadical Solver *******************************/
    void inline unit(int a)
    {
        solver_ -> clause(a);
    }

    void inline unit(int a, int assumption)
    {
        solver_ -> clause(a, assumption);
    }

    void inline binary(int a, int b)
    {   
        solver_ -> clause(a, b);
    }
    
    void inline ternary(int a, int b, int c)
    {   
        solver_ -> clause(a, b, c);
    }

    void inline quaternary(int a, int b, int c, int d)
    {   
        solver_ -> clause(a, b, c, d);
    }

    void inline create_miter(int c, int a, int b)
    {
        // c = a xor b
        // cnf: (𝑎∨𝑏∨¬𝑐)∧(¬𝑎∨¬𝑏∨¬𝑐)∧(¬𝑎∨𝑏∨𝑐)∧(𝑎∨¬𝑏∨𝑐)
        solver_ -> clause(a, b, -c);
        solver_ -> clause(-a, -b, -c);
        solver_ -> clause(-a, b, c);
        solver_ -> clause(a, -b, c);
    }

    void inline create_miter(int c, int a, int b, int assumption)
    {
        // c = a xor b
        // cnf: (𝑎∨𝑏∨¬𝑐)∧(¬𝑎∨¬𝑏∨¬𝑐)∧(¬𝑎∨𝑏∨𝑐)∧(𝑎∨¬𝑏∨𝑐)
        solver_ -> clause(a, b, -c, assumption);
        solver_ -> clause(-a, -b, -c, assumption);
        solver_ -> clause(-a, b, c, assumption);
        solver_ -> clause(a, -b, c, assumption);
    }

    void inline create_andgate(int lhs, int rhs0, int rhs1)
    {   
        // satvar_lhs = satvar_rhs0 and satvar_rhs1
        solver_ -> clause(-lhs, rhs0);
        solver_ -> clause(-lhs, rhs1);
        solver_ -> clause(lhs, -rhs0, -rhs1);
    }

    void inline create_andgate(int lhs, int rhs0, int rhs1, int assumption)
    {   
        solver_ -> clause(-lhs, rhs0, assumption);
        solver_ -> clause(-lhs, rhs1, assumption);
        solver_ -> clause(lhs, -rhs0, -rhs1, assumption);
    }
    
};





