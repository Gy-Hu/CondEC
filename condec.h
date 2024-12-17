#pragma once


#include <iostream>
#include <vector>
#include <map>


extern "C" {
#include "aiger/aiger.h"
#include "kissat_extras/src/kissat.h"
}

// #define SIM_PATTERN {0x5555555555555555UL, 0x3333333333333333UL, 0xf0f0faf0faf0f0ffUL, 0xff0aff0aff0affffUL, 0xffff0aa0ffff0f0fUL, 0xffffffffffffffffUL}
#define SIM_PATTERN {0x40CB51D950C862C9UL, 0xFFFFFFFFFFFFFFFFUL, 0xFFFFFFFFFFFFFFFFUL, 0x1515BF15BF37BE27UL, 0x1515371515BF36AFUL, 0x148D373736AF3627UL, 0x14273715368D3627UL, 0x14AFBF3736AFBEAFUL, 0x14AFBF1536AFBEAFUL, 0x14AFBE2736AF36AFUL, 0x14AF9C27BEAF14AFUL, 0x14AE8D3626AF36AEUL, 0x14AF9DBE048D3627UL, 0x14AF9D148C8CAE27UL, 0x14AF9D148C8D3627UL, 0x14AF9D9C8C273627UL, 0x14279C8C8C8D368DUL, 0x142715148CAE048DUL, 0x14AF9D148C8C0537UL, 0x159D15148C8C0537UL, 0x159D15140405148DUL, 0x151515148D9D3605UL, 0x14059D148DBF9CAFUL, 0x148D15148D9D9CAFUL, 0x148D9D159D9D9D36UL, 0x148D9D9C8D9D9C8CUL, 0x148D379C8DBF9CAEUL, 0x15BF379C8DBF1426UL, 0x14AE05BFBFBE1427UL, 0x14AE05BF9D9D1426UL, 0x14AE05BF9C8D1536UL, 0x148C05BEAF9D1514UL, 0x69F15BF0C3795AE0UL, 0x4B726240C86262C9UL, 0x417373D840C8C863UL, 0x41D8C84063D8C8C9UL, 0x4151D951FA6241D9UL, 0x4150C840C951D8C8UL, 0x4040C8BB72CCAADDUL, 0x41FAC84063726240UL, 0x41FB724173724063UL, 0x417373FAEB5062C8UL, 0x4063FBD9FB517350UL, 0x40C841FAEA404041UL, 0x40C8C8EBFA62C8EBUL, 0x415062EBFBD9FA63UL, 0x4040EBD9737350C8UL, 0x4151517373FAEB51UL, 0x40C8C9726241D973UL, 0x404173504062C972UL, 0x4151726240C8EAC9UL, 0x406263D86262EAC8UL, 0x415040EAEAEB5172UL, 0x40404150EAEAEAC9UL, 0x404173D84150C841UL, 0x4172C95062C8EAC8UL, 0x406240C9D9D8C862UL, 0x40624063D9517263UL, 0x4150C9D9FA40C862UL, 0x41FAC8EBD973D8C8UL, 0x417351D9D8C8EB50UL, 0x40C9FA63FA40EB50UL, 0x41D950C951D840C8UL, 0x415062406241D862UL}

typedef uint64_t inputs_t;
typedef uint64_t sim_hash_t;

#define SIM_ROUND 1

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

    // node <-> structural data
    std::map<unsigned, uint64_t>                node_simulation_hash_map;
    std::map<uint64_t, std::vector<unsigned>>   simulation_hash_nodevec_map;

    // node -> sim data vec
    std::map<unsigned, std::vector<uint64_t>>   node_simulation_data_map;   // node -> simulation data vec

    // sim pattern init hash
    std::vector<sim_hash_t>             sim_pattern_hash_vec;

    // create one node hash key
    int create_structural_hash(unsigned rsh0, unsigned rsh1);

    // generate 64 bit random data
    inputs_t get_random_uint64();

    // get inputs sim hash
    sim_hash_t get_sim_pattern_hash(unsigned index);

    // get simulation hash of node
    sim_hash_t get_simulation_hash(unsigned lit);

    // get simulation data of node
    inputs_t get_simulation_data(unsigned lit, int sim_round);

    // new create node number
    unsigned get_node_num(){return node_number;}

    // get the satvar from lit, lit -> node -> satvar
    int get_satvar(unsigned int lit);

    /********************************* main function for condec *****************************************/
    // cec inputs stage
    void cec_inputs_register();
    void cec_inputs_register(std::map<unsigned, uint64_t> input_cond_map);  // for input-condition

    // cec conditional outputs stage
    void cec_condition_register(unsigned int &condition_output);

    // cec and-gates stage
    void cec_ands_register();

    // cec and-gates stage
    bool cec_checker(std::vector<unsigned> &cec_candidate, unsigned &equivalence_node, int rhs0_satvar, int rhs1_satvar, int lhs_satvar);

    // create new input sim data
    void create_new_simulation_data();

    // update new sim data after create_new_simulation_data()
    void update_all_sim_data(unsigned int lhs_lit_end);


    CondEC(aiger *model, kissat *solver): model_(model), solver_(solver), node_number(0), structural_hash_merge_num(0), cec_merge_num(0), cec_sat_num(0), cec_unknow_num(0), cec_unsat_num(0),
                new_sim_data_num(0), sim_pattern_hash_vec(SIM_PATTERN), solver_satvar(0){};


    // kissat
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





