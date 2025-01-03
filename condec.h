#pragma once


#include <iostream>
#include <vector>
#include <map>


extern "C" {
#include "aiger/aiger.h"
#include "kissat_extras/src/kissat.h"
}

typedef uint64_t inputs_t;
typedef uint64_t sim_hash_t;

#define INITIAL_ROUND 10    // need to more than INITIAL_SIM_ROUND, because not every bit can sat condition
#define INITIAL_SIM_ROUND 3    // 5 sim data

class CondEC
{
private:
    /* data */
    aiger * model_;
    kissat *solver_;
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

public:
    /*  new map:
        lit             -> from aiger model
        node            -> our new model create
        satvar          -> picosat create
        structural_hash -> CondEC create
        simulation_hash -> CondEC create
        simulation_data -> CondEC create
    */
    // lit <-> node
    std::map<unsigned, unsigned>                lit_node_map;   // many lit can map to one node
    std::map<unsigned, unsigned>                node_lit_map;   // node only map to one origin lit

    // node -> satvar
    std::map<unsigned, int>                     node_satvar_map;

    // node <-> structural hash
    // std::map<unsigned, int>                     node_structural_hash_map;
    std::map<int, std::vector<unsigned>>        structural_hash_nodevec_map;

    // node <-> sim hash
    std::map<unsigned, uint64_t>                node_simulation_hash_map;
    std::map<uint64_t, std::vector<unsigned>>   simulation_hash_nodevec_map;

    // node -> sim data vec
    std::map<unsigned, std::vector<uint64_t>>   node_simulation_data_map;   // node -> simulation data vec
    std::map<unsigned, std::vector<uint64_t>>   node_cond_data_map;


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
    bool generate_initial_sim_hash_data(unsigned condition_lit);

    // cec and-gates stage
    bool cec_checker(std::vector<unsigned> &cec_candidate, unsigned &equivalence_node, int rhs0_satvar, int rhs1_satvar, int lhs_satvar);

    // create new input sim data
    void create_new_simulation_data();

    // update new sim data after create_new_simulation_data()
    void update_all_sim_data(unsigned int lhs_lit_end);


    /********************************* API *****************************************/
    // cec inputs stage
    void cec_inputs_register();

    // cec conditional outputs stage
    void cec_condition_register(unsigned int &condition_output);

    // cec and-gates stage
    void cec_ands_register();

    // cec final solve stage
    void cec_solve();

    CondEC(aiger *model, kissat *solver): model_(model), solver_(solver), node_number(0), structural_hash_merge_num(0), cec_merge_num(0), cec_sat_num(0), cec_unknow_num(0), cec_unsat_num(0),
                initial_pattern_vec(0), new_input_pattern_vec(0), new_sim_data_num(0), solver_satvar(0){};


    /********************************* KISSAT SOLVER *******************************/
    void inline unit(int a)
    {   
        kissat_add(solver_, a);
        kissat_add(solver_, 0);
    }

    void inline unit(int a, int assumption)
    {   
        kissat_add(solver_, a);
        kissat_add(solver_, assumption);
        kissat_add(solver_, 0);
    }

    void inline binary(int a, int b)
    {   
        kissat_add(solver_, a);
        kissat_add(solver_, b);
        kissat_add(solver_, 0);
    }
    
    void inline ternary(int a, int b, int c)
    {   
        kissat_add(solver_, a);
        kissat_add(solver_, b);
        kissat_add(solver_, c);
        kissat_add(solver_, 0);
    }

    void inline quaternary(int a, int b, int c, int d)
    {   
        kissat_add(solver_, a);
        kissat_add(solver_, b);
        kissat_add(solver_, c);
        kissat_add(solver_, d);
        kissat_add(solver_, 0);
    }

    void inline create_miter(int c, int a, int b)
    {
        // c = a xor b
        // cnf: (𝑎∨𝑏∨¬𝑐)∧(¬𝑎∨¬𝑏∨¬𝑐)∧(¬𝑎∨𝑏∨𝑐)∧(𝑎∨¬𝑏∨𝑐)
        ternary(a, b, -c);
        ternary(-a, -b, -c);
        ternary(-a, b, c);
        ternary(a, -b, c);
    }

    void inline create_miter(int c, int a, int b, int assumption)
    {
        quaternary(a, b, -c, assumption);
        quaternary(-a, -b, -c, assumption);
        quaternary(-a, b, c, assumption);
        quaternary(a, -b, c, assumption);
    }

    void inline create_andgate(int lhs, int rhs0, int rhs1)
    {   
        // satvar_lhs = satvar_rhs0 and satvar_rhs1
        binary(-lhs, rhs0);
        binary(-lhs, rhs1);
        ternary(lhs, -rhs0, -rhs1);
    }

    void inline create_andgate(int lhs, int rhs0, int rhs1, int assumption)
    {   
        ternary(-lhs, rhs0, assumption);
        ternary(-lhs, rhs1, assumption);
        quaternary(lhs, -rhs0, -rhs1, assumption);

    }
    
};





