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

void create_aiger_after_condec(aiger * model, CondEC &condeq_check, const char *new_file){  // for conditional cec create new aig
    aiger * new_model;
    new_model = aiger_init();
    std::map<unsigned, unsigned> node_var_map;

    for(int i = 0; i < model -> num_inputs; i ++){
        auto input_lit = model->inputs[i].lit;
        auto input_node = condeq_check.lit_node_map[input_lit];
        aiger_add_input(new_model, aiger_var2lit(input_node), 0);

        node_var_map[input_node] = new_model->maxvar;
    }
    for(int i = 0; i < model -> num_ands; i ++){
        auto rhs0_lit = model -> ands[i].rhs0;
        auto rhs1_lit = model -> ands[i].rhs1;
        auto lhs_lit  = model -> ands[i].lhs;

        auto rhs0_node = condeq_check.lit_node_map[aiger_strip(rhs0_lit)];
        auto rhs1_node = condeq_check.lit_node_map[aiger_strip(rhs1_lit)];
        auto lhs_node  = condeq_check.lit_node_map[lhs_lit];
        
        if(node_var_map.find(lhs_node) != node_var_map.end()){
            continue;
        }
        auto maxvar = (new_model -> maxvar) + 1;

        auto rhs0_new_lit = aiger_sign(rhs0_lit) ? aiger_var2lit(node_var_map[rhs0_node])+1 : aiger_var2lit(node_var_map[rhs0_node]);
        auto rhs1_new_lit = aiger_sign(rhs1_lit) ? aiger_var2lit(node_var_map[rhs1_node])+1 : aiger_var2lit(node_var_map[rhs1_node]);
        auto lhs_new_lit = aiger_var2lit(maxvar);

        aiger_add_and(new_model, lhs_new_lit, rhs0_new_lit, rhs1_new_lit);
        node_var_map[lhs_node] = new_model->maxvar;
    }
    for(int i = 0; i < model -> num_outputs; i ++){
        auto output_lit = model->outputs[i].lit;
        auto output_node = condeq_check.lit_node_map[aiger_strip(output_lit)];
        auto output_new_lit = aiger_sign(output_lit) ? aiger_var2lit(node_var_map[output_node])+1 : aiger_var2lit(node_var_map[output_node]);

        aiger_add_output(new_model, output_new_lit, 0);
    }

    std::cout << "new aiger model MIOA:" << std::endl;
    std::cout << "M: " << new_model -> maxvar << std::endl;
    std::cout << "I: " << new_model -> num_inputs << std::endl;
    std::cout << "0: " << new_model -> num_outputs << std::endl;
    std::cout << "A: " << new_model -> num_ands << std::endl;

    // merge 2 output
    aiger_add_and(new_model, aiger_var2lit(new_model->maxvar + 1), new_model -> outputs[0].lit, new_model -> outputs[1].lit);
    new_model->num_outputs = 1;
    new_model->outputs[0].lit = aiger_var2lit(new_model->maxvar);

    FILE *new_aig_merge = fopen (new_file, "w");
    aiger_write_to_file(new_model, aiger_binary_mode, new_aig_merge);    // triggers 'aig_reencode'

    std::cout << "after reencode new aiger model MIOA:" << std::endl;
    std::cout << "M: " << new_model -> maxvar << std::endl;
    std::cout << "I: " << new_model -> num_inputs << std::endl;
    std::cout << "0: " << new_model -> num_outputs << std::endl;
    std::cout << "A: " << new_model -> num_ands << std::endl;

    aiger_reset(new_model);
}

int main(int argc, char ** argv) {

    static aiger * model;
    model = aiger_init ();

    static const char * name;
    const char * err;
    int i, j;

    name = argv[1];
    if (name) err = aiger_open_and_read_from_file (model, name);

    printf ("MILOA = %u %u %u %u %u\n",
       model->maxvar,
       model->num_inputs,
       model->num_latches,
       model->num_outputs,
       model->num_ands);

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

    // merge 2 condition outputs to 1 condition output 
    aiger_add_and(model, aiger_var2lit(model->maxvar + 1), model -> outputs[1].lit, model -> outputs[2].lit);
    model->num_outputs = 2;
    model->outputs[1].lit = aiger_var2lit(model->maxvar);
    aiger_reencode(model);

    // create output+condition aig
        // FILE *output2_aig = fopen ("output2_aig", "w");
        // aiger_write_to_file(model, aiger_binary_mode, output2_aig);
        // use aigsplit to get cond.aig
        // use abc to transfer cond.aig to cond.cnf
        // use cryptominisat to sat cond.cnf for getting initial sim hash and sim data

    // conditonal equivalence checking
    unsigned int miter_output = model -> outputs[0].lit;
    auto condition_output = model -> outputs[1].lit;
    
auto clk_start = std::chrono::high_resolution_clock::now();

    kissat *solver;
    solver = kissat_init();
    kissat_set_option(solver, "quiet", 1);  // stop print kissat log
    CondEC condeq_check(model, solver);
    condeq_check.cec_inputs_register();
    condeq_check.cec_condition_register(condition_output);
    condeq_check.cec_ands_register();

auto clk_end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> cec_time = clk_end - clk_start;
    std::cout << "conditional equilvalence time: " << cec_time.count() << " seconds\n";

    // const char *new_file = argv[2];
    const char *new_file = "new.aig";
    create_aiger_after_condec(model, condeq_check, new_file); // after condec, we merge condition and output and create new aig

    kissat_release(solver);
    aiger_reset(model);
    return 0;
    
}
