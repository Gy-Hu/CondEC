#pragma once


#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>

#include "cadical/src/cadical.hpp"

extern "C" {
#include "aiger.h"
}

typedef uint64_t inputs_t;
typedef uint64_t sim_hash_t;

#define INITIAL_ROUND 15    // need to more than INITIAL_SIM_ROUND, because not every bit can sat condition
#define INITIAL_SIM_ROUND 10    // sim data

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
    int create_satvar(){ return ++solver_satvar;}
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

    // lit <-> node
    std::unordered_map<unsigned, node_neg>                lit_node_map;   // many lit can map to one node
    std::unordered_map<unsigned, unsigned>                node_lit_map;   // node only map to one origin lit

    // node -> satvar
    std::unordered_map<unsigned, int>                     node_satvar_map;

    // node <-> structural hash
    std::unordered_map<int, std::vector<unsigned>>        structural_hash_nodevec_map;

    // node <-> sim hash
    std::unordered_map<unsigned, sim_hash_t>                node_simulation_hash_map;
    std::unordered_map<sim_hash_t, std::vector<unsigned>>   simulation_hash_nodevec_map;

    // node -> sim data vec
    std::unordered_map<unsigned, std::vector<inputs_t>>   node_simulation_data_map;   // node -> simulation data vec
    std::unordered_map<unsigned, std::vector<inputs_t>>   node_cond_data_map;


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

    CondEC(aiger *model, CaDiCaL::Solver *solver): model_(model), solver_(solver), node_number(0), structural_hash_merge_num(0), cec_merge_num(0), cec_sat_num(0), cec_unknow_num(0), cec_unsat_num(0),
                initial_pattern_vec(0), new_input_pattern_vec(0), new_sim_data_num(0), solver_satvar(0){};


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





