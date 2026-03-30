#include <iostream>
#include <vector>
#include <queue>
#include <map>
#include <cassert>
#include <chrono>
#include <functional>

#include "node_condec.h"

extern "C" {
#include "aiger.h"
}

int main(int argc, char ** argv) {

    static aiger * model;
    model = aiger_init ();

    static const char * name;
    const char * err;
    int i, j;

    if(argc == 1){
        std::cout << "[Usage] " << argv[0] << " <aigfile> [-option]" << std::endl;
        return 1;
    }
    for(int i = 1; i < argc; i++){
        std::string var = argv[i];
        if(var == "-q"){
            std::cout.setstate(std::ios::failbit);
        }
        else if(var == "-h"){
            std::cout << "[Usage] " << argv[0] << " <aigfile> [-option]" << std::endl;
            std::cout << "common options: " << std::endl;
            std::cout << "       -h     print this list of common command line options" << std::endl;
            std::cout << "       -q     be quiet, only print check time" << std::endl;
            return 1;
        }
        else
            name = argv[i];
    }

    if (name) err = aiger_open_and_read_from_file (model, name);
    if (err) {std::cout << "[Error] " << err << std::endl; return 1;}

    if(model->num_constraints == 0){
        std::cout << "[Error] Minimum 1 constraint required." << std::endl;
        return 1;
    }

    std::cout << "maxvar = " << model->maxvar << std::endl;
    std::cout << "inputs = " << model->num_inputs << std::endl;
    std::cout << "latches = " << model->num_latches << std::endl;
    std::cout << "outputs = " << model->num_outputs << std::endl;
    std::cout << "ands = " << model->num_ands << std::endl;
    std::cout << "bad = " << model->num_bad << std::endl;
    std::cout << "constraints = " << model->num_constraints << std::endl;


    // conditonal equivalence checking

    // preprocess miter output and conditions
    unsigned int miter_o = model -> outputs[0].lit;
    std::vector<int> condition_vec;

    // recursively flatten positive AND trees to extract leaf conditions
    // only descend into non-negated AND children; stop at negated or input lits
    // Use original one-level decomposition for each constraint (handles negated AND
    // via De Morgan at one level only), then recursively flatten any positive AND children
    std::function<void(unsigned)> flatten_positive_and = [&](unsigned lit) {
        if (lit == 0 || lit == 1) return;
        if (aiger_sign(lit)) {
            // negated literal: leaf, don't decompose further
            condition_vec.push_back(lit);
            return;
        }
        aiger_and *gate = aiger_is_and(model, lit);
        if (!gate) {
            // input: leaf
            condition_vec.push_back(lit);
            return;
        }
        // positive AND gate: recurse into both children
        flatten_positive_and(gate->rhs0);
        flatten_positive_and(gate->rhs1);
    };

    for(int i = 0; i < model->num_constraints; i++){
        aiger_and *cur_and_gate = aiger_is_and(model, model->constraints[i].lit);
        auto neg = aiger_sign(model->constraints[i].lit);

        if (!cur_and_gate) {
            // constraint is a plain input literal
            condition_vec.push_back(model->constraints[i].lit);
            continue;
        }

        int cond0, cond1;
        auto sign0 = aiger_sign(cur_and_gate->rhs0);
        auto sign1 = aiger_sign(cur_and_gate->rhs1);
        if(neg == 0){
            cond0 = cur_and_gate->rhs0;
            cond1 = cur_and_gate->rhs1;
        }
        else{
            cond0 = sign0? cur_and_gate->rhs0 - 1 : cur_and_gate->rhs0 + 1;
            cond1 = sign1? cur_and_gate->rhs1 - 1 : cur_and_gate->rhs1 + 1;
        }
        if(cur_and_gate->rhs0 == 1){
            flatten_positive_and(cond1);
        }
        else if(cur_and_gate->rhs1 == 1){
            flatten_positive_and(cond0);
        }
        else{
            flatten_positive_and(cond0);
            flatten_positive_and(cond1);
        }
    }

    unsigned int condition = condition_vec.at(0);

    if(condition_vec.size() > 1){
        for(int cond_idx = 1; cond_idx < condition_vec.size(); cond_idx++){
            aiger_add_and(model, aiger_var2lit(model->maxvar + 1), condition, condition_vec.at(cond_idx));
            condition = aiger_var2lit(model->maxvar);
        }
        // aiger_reencode(model);   // it will cause maxvar reset to origin
    }
    
    
    std::cout << "after preprocess maxvar = " << model->maxvar << std::endl;
    std::cout << "after preprocess ands = " << model->num_ands << std::endl;
    
    // return 0;
    // // test
    // auto and_gate = aiger_is_and(model, model->constraints[0].lit);
    // std::cout << "lhs = " << and_gate->lhs << std::endl;
    // std::cout << "rhs0 = " << and_gate->rhs0 << std::endl;
    // std::cout << "rhs1 = " << and_gate->rhs1 << std::endl;
    // std::cout << aiger_strip(0) << std::endl;
    // std::cout << aiger_sign(0) << std::endl;
    // std::cout << aiger_strip(1) << std::endl;
    // std::cout << aiger_sign(1) << std::endl;
    // std::cout << aiger_is_constant(1) << std::endl;
    // std::cout << aiger_is_constant(8935) << std::endl;
    
    

auto clk_start = std::chrono::high_resolution_clock::now();

    // init solver
    CaDiCaL::Solver *solver = new CaDiCaL::Solver;

    // tune solver for incremental equivalence checking workload
    solver->set("walk", 0);          // disable SLS walking (not useful for proving UNSAT)
    solver->set("condition", 0);     // disable global blocked clause elimination (overhead)

    // main stage
    CondEC condeq_check(model, solver);
    condeq_check.cec_inputs_register();
    condeq_check.cec_condition_register(condition, condition_vec);
    condeq_check.cec_ands_register();
    auto res = condeq_check.cec_solve();

    std::string sat_res;
    if(res == 10)
        sat_res = "SAT";
    else if(res == 20)
        sat_res = "UNSAT";
    else
        sat_res = "UNKNOW";

auto clk_end = std::chrono::high_resolution_clock::now();

    // print stat
    std::chrono::duration<double> cec_time = clk_end - clk_start;
    std::cout.clear();
    std::cout << name << ", " << sat_res << ", time: " << cec_time.count() << " s\n";

    // release solver and aiger
    aiger_reset(model);
    return 0;
    
}
