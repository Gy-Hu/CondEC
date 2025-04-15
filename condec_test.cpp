#include <iostream>
#include <vector>
#include <queue>
#include <map>
#include <cassert>
#include <chrono>

#include "condec.h"

extern "C" {
#include "aiger/aiger.h"
#include "kissat_extras/src/kissat.h"
}


int compute_depth(unsigned lit, aiger *model, std::map<int, int> &depth_map) {
    
    unsigned var = aiger_lit2var(lit);

    // lit = 0, depth = 0
    if (lit == 0) return 0;

    // if lit already compute, return cache
    if (depth_map.at(var) >= 0) return depth_map[var];

    // if lit is inputs, depth = 0
    if (var <= model->num_inputs) {
        depth_map[var] = 0;
        return 0;
    }

    // get the and gate of lit
    aiger_and *and_gate = aiger_is_and(model, lit);

    if (!and_gate) {
        fprintf(stderr, "Error: invalid literal %u\n", lit);
        exit(1);
    }

    // compute depth of and gate child (can use stack later)
    int depth_rhs0 = compute_depth(and_gate->rhs0, model, depth_map);
    int depth_rhs1 = compute_depth(and_gate->rhs1, model, depth_map);

    // current node depth is max child depth + 1
    int depth = 1 + (depth_rhs0 > depth_rhs1 ? depth_rhs0 : depth_rhs1);
    depth_map[var] = depth;

    return depth;
}

// void create_aiger_after_condec(aiger * model, CondEC &condeq_check, const char *new_file){  // for conditional cec create new aig
//     aiger * new_model;
//     new_model = aiger_init();
//     std::map<unsigned, unsigned> node_var_map;

//     for(int i = 0; i < model -> num_inputs; i ++){
//         auto input_lit = model->inputs[i].lit;
//         auto input_node = condeq_check.lit_node_map[input_lit];
//         aiger_add_input(new_model, aiger_var2lit(input_node), 0);

//         node_var_map[input_node] = new_model->maxvar;
//     }
//     for(int i = 0; i < model -> num_ands; i ++){
//         auto rhs0_lit = model -> ands[i].rhs0;
//         auto rhs1_lit = model -> ands[i].rhs1;
//         auto lhs_lit  = model -> ands[i].lhs;

//         auto rhs0_node = condeq_check.lit_node_map[aiger_strip(rhs0_lit)];
//         auto rhs1_node = condeq_check.lit_node_map[aiger_strip(rhs1_lit)];
//         auto lhs_node  = condeq_check.lit_node_map[lhs_lit];
        
//         if(node_var_map.find(lhs_node) != node_var_map.end()){
//             continue;
//         }
//         auto maxvar = (new_model -> maxvar) + 1;

//         auto rhs0_new_lit = aiger_sign(rhs0_lit) ? aiger_var2lit(node_var_map[rhs0_node])+1 : aiger_var2lit(node_var_map[rhs0_node]);
//         auto rhs1_new_lit = aiger_sign(rhs1_lit) ? aiger_var2lit(node_var_map[rhs1_node])+1 : aiger_var2lit(node_var_map[rhs1_node]);
//         auto lhs_new_lit = aiger_var2lit(maxvar);

//         aiger_add_and(new_model, lhs_new_lit, rhs0_new_lit, rhs1_new_lit);
//         node_var_map[lhs_node] = new_model->maxvar;
//     }
//     for(int i = 0; i < model -> num_outputs; i ++){
//         auto output_lit = model->outputs[i].lit;
//         auto output_node = condeq_check.lit_node_map[aiger_strip(output_lit)];
//         auto output_new_lit = aiger_sign(output_lit) ? aiger_var2lit(node_var_map[output_node])+1 : aiger_var2lit(node_var_map[output_node]);

//         aiger_add_output(new_model, output_new_lit, 0);
//     }

//     std::cout << "new aiger model MIOA:" << std::endl;
//     std::cout << "M: " << new_model -> maxvar << std::endl;
//     std::cout << "I: " << new_model -> num_inputs << std::endl;
//     std::cout << "0: " << new_model -> num_outputs << std::endl;
//     std::cout << "A: " << new_model -> num_ands << std::endl;

//     // merge 2 output
//     aiger_add_and(new_model, aiger_var2lit(new_model->maxvar + 1), new_model -> outputs[0].lit, new_model -> outputs[1].lit);
//     new_model->num_outputs = 1;
//     new_model->outputs[0].lit = aiger_var2lit(new_model->maxvar);

//     FILE *new_aig_merge = fopen (new_file, "w");
//     aiger_write_to_file(new_model, aiger_binary_mode, new_aig_merge);    // triggers 'aig_reencode'

//     std::cout << "after reencode new aiger model MIOA:" << std::endl;
//     std::cout << "M: " << new_model -> maxvar << std::endl;
//     std::cout << "I: " << new_model -> num_inputs << std::endl;
//     std::cout << "0: " << new_model -> num_outputs << std::endl;
//     std::cout << "A: " << new_model -> num_ands << std::endl;

//     aiger_reset(new_model);
// }

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

    if(model->num_outputs <= 1){
        std::cout << "[Error] Minimum 2 outputs required." << std::endl;
        return 1;
    }

    std::cout << "maxvar = " << model->maxvar << std::endl;
    std::cout << "inputs = " << model->num_inputs << std::endl;
    std::cout << "latches = " << model->num_latches << std::endl;
    std::cout << "outputs = " << model->num_outputs << std::endl;
    std::cout << "ands = " << model->num_ands << std::endl;
    std::cout << "bad = " << model->num_bad << std::endl;
    std::cout << "constraints = " << model->num_constraints << std::endl;

    // find which is output, which is conditon，by computing depth of outputs
    std::map<int, int> depth_map;
    int max_depth = 0;

    for (unsigned i = 1; i <= model ->maxvar +1; i++) {
        depth_map[i] = -1;  // initialize depth map, -1 = no compute
    }

    for(int i = 0; i < model -> num_outputs; i ++){
        auto output_lit = model -> outputs[i].lit;
        int depth = compute_depth(output_lit, model, depth_map);
        max_depth = (depth > max_depth) ? depth : max_depth;
        std::cout << "Depth of output " << output_lit << ": " << depth << std::endl;
    }
    std::cout << "Max Depth of output: " << max_depth << std::endl;

    // get all condition lit
    std::vector<int> condition_vec;
    for(int i = 1; i < model -> num_outputs; i ++){
        condition_vec.push_back(model->outputs[i].lit);
    }

    // merge 2 condition outputs to 1 condition output
    if( model->num_outputs > 2){
        for(int i = 2; i < model->num_outputs; i++){
            aiger_add_and(model, aiger_var2lit(model->maxvar + 1), model -> outputs[1].lit, model -> outputs[i].lit);
            model->outputs[1].lit = aiger_var2lit(model->maxvar);
        }
        model->num_outputs = 2;
        aiger_reencode(model);
    }

    // conditonal equivalence checking
    unsigned int miter_output = model -> outputs[0].lit;
    auto condition_output = model -> outputs[1].lit;
    
auto clk_start = std::chrono::high_resolution_clock::now();

    kissat *solver;
    solver = kissat_init();
    kissat_set_option(solver, "quiet", 1);  // stop print kissat log
    
    CondEC condeq_check(model, solver);
    condeq_check.cec_inputs_register();
    condeq_check.cec_condition_register(condition_output, condition_vec);
    // add all conditions separately to the solver, it will sat faster
    if(condition_vec.size() > 10){
        for(auto &cond_output : condition_vec){
            auto cond_node = condeq_check.lit_node_map[aiger_strip(cond_output)];
            auto cond_satvar = aiger_sign(cond_output) ? -condeq_check.node_satvar_map[cond_node.node] : condeq_check.node_satvar_map[cond_node.node];
            cond_satvar = cond_node.neg ? ~cond_satvar : cond_satvar;
            condeq_check.unit(cond_satvar);
        }
    }
    condeq_check.cec_ands_register();
    condeq_check.cec_solve();

auto clk_end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> cec_time = clk_end - clk_start;
    std::cout.clear();
    std::cout << "conditional equilvalence time: " << cec_time.count() << " seconds\n";

    kissat_release(solver);
    aiger_reset(model);
    return 0;
    
}
