#include <iostream>
#include <vector>
#include <map>
#include <stack>
#include <random>
#include <cassert>
#include <chrono>
#include <algorithm>

#include "node_condec.h"

extern "C" {
#include "aiger.h"
}

/*  create a hash for and-gates node (lhs)
    structural node hash = f(id1, id2, c1, c2)
*/
int CondEC::create_structural_hash(unsigned rsh0, unsigned rsh1){
    auto id1 = lit_node_map[aiger_strip(rsh0)];
    auto id2 = lit_node_map[aiger_strip(rsh1)];

    bool c1 = aiger_sign(rsh0); // true = neg, false = pos
    bool c2 = aiger_sign(rsh1); // true = neg, false = pos
    c1 = id1.neg ? !c1 : c1;
    c2 = id2.neg ? !c2 : c2;

    // normalize operand order so AND(a,b) and AND(b,a) get the same hash
    if (id1.node > id2.node || (id1.node == id2.node && c1 > c2)) {
        std::swap(id1, id2);
        std::swap(c1, c2);
    }

    unsigned hash = 0;
    hash += id1.node * 7937;
    hash += id2.node * 2971;
    hash += c1  * 911;
    hash += c2  * 353;
    hash -= 2011;

    return (int)(hash);
}

inputs_t CondEC::get_random_uint64()
{
    static std::random_device rd;
    // static std::mt19937_64 gen(rd());
    static std::mt19937_64 gen(1000007);
    return gen();
}

//  get simulation hash or ~hash
sim_hash_t CondEC::get_simulation_hash(unsigned lit){
    bool neg = aiger_sign(lit);
    auto node = lit_node_map[aiger_strip(lit)];
    auto sim_hash = neg ? ~node_simulation_hash_map[node.node] : node_simulation_hash_map[node.node];
    sim_hash = node.neg ? ~sim_hash : sim_hash;
    return sim_hash;
}

//  get simulation data or ~data
inputs_t CondEC::get_simulation_data(unsigned lit, int sim_round){
    bool neg = aiger_sign(lit);
    auto node = lit_node_map[aiger_strip(lit)];
    auto sim_data = neg? ~node_simulation_data_map[node.node].at(sim_round) : node_simulation_data_map[node.node].at(sim_round);
    sim_data = node.neg ? ~sim_data : sim_data;
    return sim_data;
}

// get satvar
int CondEC::get_satvar(unsigned int lit){
    auto node = lit_node_map[aiger_strip(lit)];
    auto satvar = aiger_sign(lit) ?  -node_satvar_map[node.node] : node_satvar_map[node.node];
    satvar = node.neg ? -satvar : satvar;
    return satvar;
}

bool CondEC::generate_initial_sim_hash_data(unsigned condition_lit, std::vector<int> condition_vec){
    int initial_sim_round = 0;
    initial_pattern_vec.resize(model_->num_inputs);
    while(initial_pattern_vec.at(0).size() <= (INITIAL_SIM_ROUND+1) * 64){  // we need to generate 1 group sim hash and 5 group sim data, (1+5)*64 bit
        // random input data
        for(int i = 0; i < model_ -> num_inputs; i ++){
            auto input_lit = model_ -> inputs[i].lit;
            auto input_node = lit_node_map[input_lit];
            // if aiger have input condition, we set the condition data
            bool set_sim_data = false;
            uint64_t sim_data_condition;
            for(int i = 0; i < condition_vec.size(); i++){
                if(input_lit == aiger_strip(condition_vec[i])){
                    sim_data_condition = aiger_sign(condition_vec[i]) ? 0x0000000000000000UL : 0xFFFFFFFFFFFFFFFFUL;
                    // std::cout << "input_lit: " << input_lit << std::endl;
                    // std::cout << "sim_data_condition: " << sim_data_condition << std::endl;
                    set_sim_data = true;
                    break;
                }
            }

            for(int round = 0; round < INITIAL_ROUND; round++){
                auto random_data = get_random_uint64();
                auto sim_data = set_sim_data ? sim_data_condition: random_data;
                // std::cout << "input_lit: " << input_lit << std::endl;
                // std::cout << "sim_data: " << sim_data << std::endl;
                node_cond_data_map[input_node.node].push_back(sim_data);
            }
        }

        // simulation
        for(int i = 0; i < model_->num_ands; i++){
            auto rhs0_lit = model_ -> ands[i].rhs0;
            auto rhs1_lit = model_ -> ands[i].rhs1;
            auto lhs_lit  = model_ -> ands[i].lhs;

            if(!lit_has_node(lhs_lit)){
                // this lit is not in condition
                continue;
            }
                // this lit is in condition
            auto rhs0_node = lit_node_map[aiger_strip(rhs0_lit)];
            auto rhs1_node = lit_node_map[aiger_strip(rhs1_lit)];
            auto lhs_node = lit_node_map[aiger_strip(lhs_lit)];

            for(int cond_sim_round = initial_sim_round; cond_sim_round < initial_sim_round + INITIAL_ROUND; cond_sim_round++){
                auto rhs0_sim_data = aiger_sign(rhs0_lit) ? ~node_cond_data_map[rhs0_node.node].at(cond_sim_round) : node_cond_data_map[rhs0_node.node].at(cond_sim_round);
                rhs0_sim_data = rhs0_node.neg ? ~rhs0_sim_data : rhs0_sim_data;
                auto rhs1_sim_data = aiger_sign(rhs1_lit) ? ~node_cond_data_map[rhs1_node.node].at(cond_sim_round) : node_cond_data_map[rhs1_node.node].at(cond_sim_round);
                rhs1_sim_data = rhs1_node.neg ? ~rhs1_sim_data : rhs1_sim_data;
                auto lhs_sim_data = rhs0_sim_data & rhs1_sim_data;
                node_cond_data_map[lhs_node.node].push_back(lhs_sim_data);
            }
        }

        // get the sat solutions
        auto condition_node = lit_node_map[aiger_strip(condition_lit)];
        for(int cond_sim_round = initial_sim_round; cond_sim_round < initial_sim_round + INITIAL_ROUND; cond_sim_round++){
            auto condition_sim_data = aiger_sign(condition_lit) ?  ~node_cond_data_map[condition_node.node].at(cond_sim_round) : node_cond_data_map[condition_node.node].at(cond_sim_round);
            condition_sim_data = condition_node.neg ? ~condition_sim_data : condition_sim_data;
            for (int i = 0; i < 64; ++i) {
                if (condition_sim_data & (1ULL << i)) { // if sim data bit == 1
                    for(int j = 0; j < model_ -> num_inputs; j ++){
                        auto input_lit = model_ -> inputs[j].lit;
                        auto input_node = lit_node_map[input_lit];
                        auto input_bit = node_cond_data_map[input_node.node].at(cond_sim_round) & (1ULL << i);
                        initial_pattern_vec.at(j).push_back(input_bit);
                    }
                }
            }
        }

        std::cout << "we already generate useful initial pattern size: " << initial_pattern_vec.at(0).size() << std::endl;
        initial_sim_round = initial_sim_round + INITIAL_ROUND;

        // only give up if we've tried multiple rounds and still can't get enough valid patterns
        if(initial_sim_round >= INITIAL_ROUND * 5 && initial_pattern_vec.at(0).size() <= 64){
            return true;
        }
    }
    
    // transfer initial_pattern_vec to node_simulation_data_map
    uint64_t sim_hash = 0;
    uint64_t sim_data = 0;
    for(int i = 0; i < model_ -> num_inputs; i ++){
        int initial_sim_num = 0;
        auto input_node = lit_node_map[model_->inputs[i].lit];

        sim_hash = 0;
        for(int pattern_round = 0; pattern_round < 64; pattern_round++){
            sim_hash <<= 1;
            sim_hash |= (initial_pattern_vec.at(i).at(pattern_round) ? 1 : 0);
        }
        node_simulation_hash_map[input_node.node] = sim_hash;

        sim_data = 0;
        for(int pattern_round = 64; pattern_round < (INITIAL_SIM_ROUND+1) * 64; pattern_round++){
            sim_data <<= 1;
            sim_data |= (initial_pattern_vec.at(i).at(pattern_round) ? 1 : 0);
            initial_sim_num++;
            if(initial_sim_num == 64){
                auto input_node = lit_node_map[model_->inputs[i].lit];
                node_simulation_data_map[input_node.node].push_back(sim_data);
                initial_sim_num = 0;
            }
        }
        
        assert(node_simulation_data_map[input_node.node].size() == INITIAL_SIM_ROUND);
    }

    return false;
}


// after cec_checker if have 64 sat solutions, create a new sim data for input
void CondEC::create_new_simulation_data(){
    // bit2data
    inputs_t new_sim_data = 0;
    unsigned count = 0;
    for(int i = 0; i < new_input_pattern_vec.size(); i++){
        for(int pattern_round = new_sim_data_num * 64; pattern_round < (new_sim_data_num + 1) * 64; pattern_round++){
            new_sim_data <<= 1;
            new_sim_data |= (new_input_pattern_vec.at(i).at(pattern_round) ? 1 : 0);
        }
        auto input_node = lit_node_map[model_->inputs[i].lit];
        node_simulation_data_map[input_node.node].push_back(new_sim_data);
        // std::cout << "input node " << input_node << " new sim data: " << new_sim_data << std::endl;
    }
    
    new_sim_data_num++;
}

// update new round sim data for all created nodes
void CondEC::update_all_sim_data(unsigned int lhs_lit_end){
    for(int i = 0; i < model_->num_ands; i++){
        if((model_->ands[i].lhs) > lhs_lit_end){
            // update all created and-gates
            break;
        }
        auto rhs0_lit = model_ -> ands[i].rhs0;
        auto rhs1_lit = model_ -> ands[i].rhs1;
        auto lhs_lit  = model_ -> ands[i].lhs;

        auto rhs0_sim_data = get_simulation_data(rhs0_lit, INITIAL_SIM_ROUND + new_sim_data_num - 1);
        auto rhs1_sim_data = get_simulation_data(rhs1_lit, INITIAL_SIM_ROUND + new_sim_data_num - 1);
        auto sim_data = rhs0_sim_data & rhs1_sim_data;

        auto and_node = lit_node_map[lhs_lit];
        if(node_simulation_data_map[and_node.node].size() == INITIAL_SIM_ROUND + new_sim_data_num - 1){
            node_simulation_data_map[and_node.node].push_back(sim_data);
        }
        
    }

}

// check equivalence node
bool CondEC::cec_checker(const std::vector<unsigned>& cec_candidate, unsigned &equivalence_node, int rhs0_satvar, int rhs1_satvar, int lhs_satvar){
    for(auto &other_node : cec_candidate){
        // assume
        auto assumption = create_satvar();
        
        create_andgate(lhs_satvar, rhs0_satvar, rhs1_satvar, assumption); // clause OR assumption
        auto miter_i1 = node_satvar_map[other_node];
        auto miter_i2 = lhs_satvar;
        auto miter_o = create_satvar();
        create_miter(miter_o, miter_i1, miter_i2, assumption);  // clause OR assumption

        // unit(miter_o, assumption);  // clause OR assumption
        solver_->assume(miter_o);
        solver_->assume(-assumption);    // make assumption = flase,

        solver_ ->limit("conflicts", 10000);
        int res = solver_ -> solve();

        if(res == 10){
            // add more sim round and sim data
            cec_sat_num++;
            if (new_input_pattern_vec.empty())
                new_input_pattern_vec.resize(model_->num_inputs);   // set input number vector
            for(int i = 0; i < model_ -> num_inputs; i ++){
                auto input_lit = model_ -> inputs[i].lit;
                auto input_node = lit_node_map[input_lit];
                auto input_satvar = node_satvar_map[input_node.node];

                int sat_assignment = solver_ -> val(input_satvar);
                bool sim_bit = (sat_assignment > 0) ? 1 : 0;
                new_input_pattern_vec.at(i).push_back(sim_bit);
            }
        }
        else if(res == 20){
            // merge equivalence node
            cec_unsat_num++;
            equivalence_node = other_node;
            unit(assumption);   // make assumption = true, disable clause of this function
            return true;    // merge
        }
        else{
            // nothing to do
            cec_unknow_num++;
        }

        unit(assumption);   // make assumption = true, disable clause of this function
    }

    return false;   // no merge
}

bool CondEC::cec_checker_neg(const std::vector<unsigned>& cec_candidate, unsigned &equivalence_node, int rhs0_satvar, int rhs1_satvar, int lhs_satvar){
    for(auto &other_node : cec_candidate){
        // assume
        auto assumption = create_satvar();
        
        create_andgate(lhs_satvar, rhs0_satvar, rhs1_satvar, assumption); // clause OR assumption
        auto miter_i1 = node_satvar_map[other_node];
        auto miter_i2 = lhs_satvar;
        auto miter_o = create_satvar();
        create_miter(miter_o, -miter_i1, miter_i2, assumption);  // clause OR assumption
        
        solver_->assume(miter_o);
        solver_->assume(-assumption);    // make assumption = flase,

        solver_ ->limit("conflicts", 10000);
        int res = solver_ -> solve();

        if(res == 10){
            // add more sim round and sim data
            cec_sat_num++;
            if (new_input_pattern_vec.empty())
                new_input_pattern_vec.resize(model_->num_inputs);   // set input number vector
            for(int i = 0; i < model_ -> num_inputs; i ++){
                auto input_lit = model_ -> inputs[i].lit;
                auto input_node = lit_node_map[input_lit];
                auto input_satvar = node_satvar_map[input_node.node];

                int sat_assignment = solver_ -> val(input_satvar);
                bool sim_bit = (sat_assignment > 0) ? 1 : 0;
                new_input_pattern_vec.at(i).push_back(sim_bit);
            }
        }
        else if(res == 20){
            // merge equivalence node
            cec_unsat_num++;
            equivalence_node = other_node;
            unit(assumption);   // make assumption = true, disable clause of this function
            return true;    // merge
        }
        else{
            // nothing to do
            cec_unknow_num++;
        }

        unit(assumption);   // make assumption = true, disable clause of this function
    }

    return false;   // no merge
}

void CondEC::cec_inputs_register(){
    for(int i = 0; i < model_ -> num_inputs; i ++){
        // get the lit from aiger model
        auto input_lit = model_ -> inputs[i].lit;

        // lit <-> node
        auto input_node = create_new_node();
        node_neg input_node_pn = {input_node, 0};
        lit_node_map[input_lit] = input_node_pn;
        node_lit_map[input_node] = input_lit;

        // node -> sat var
        auto satvar = create_satvar();
        node_satvar_map[input_node] = satvar;

        // freeze input variables to prevent elimination by solver
        solver_->freeze(satvar);
    }
}

void CondEC::cec_condition_register(unsigned int &condition_output, std::vector<int> &condition_vec){
    // get the condition lit 
    auto condition_lit = condition_output;
    int condition_node_number = 0;

    // post-order traverse
    std::stack<std::pair<unsigned, int>> ands_stack;
    ands_stack.push({condition_lit, 0});

    while(!ands_stack.empty()){
        auto [cur_lit, index] = ands_stack.top();   // cur_lit is odd or even
        
        // if lit is input, directly pop
        if(cur_lit <= aiger_var2lit(model_->num_inputs) + 1){   
            ands_stack.pop();
            continue;
        }

        // when lit already create node, jump to next turn
        if(lit_has_node(cur_lit)){
                // std::cout << "find same and-gates node" << std::endl;
                ands_stack.pop();
                continue;
        }

        // if lit is and-gate, push child when no all child are pushed to stack
        if(index < 2){
            aiger_and *cur_and_gate = aiger_is_and(model_, cur_lit);
            ands_stack.top().second++;
            auto child_lit = (index == 0)? cur_and_gate->rhs0 : cur_and_gate->rhs1;
            ands_stack.push({child_lit, 0});
        }
        // if lit is and-gate, create node when all child are pushed in stack
        else{
            ands_stack.pop();

            // get the lit from aiger model
            aiger_and *cur_and_gate = aiger_is_and(model_, cur_lit);
            cur_lit = cur_and_gate -> lhs;  // cur_lit update to even

            // lit <-> node
            auto and_node = create_new_node();
            node_neg and_node_pn = {and_node, 0};
            lit_node_map[cur_lit] = and_node_pn;
            // node_lit_map[and_node] = cur_lit;

            // node <-> sat var
            auto satvar = create_satvar();
            node_satvar_map[and_node] = satvar;

            // add cnf
            auto rhs0_satvar = get_satvar(cur_and_gate->rhs0);
            auto rhs1_satvar = get_satvar(cur_and_gate->rhs1);
            create_andgate(satvar, rhs0_satvar, rhs1_satvar);

            // for test
            condition_node_number++;
        }

    }// end of while

    // add condition to picosat
    // auto condition_node = lit_node_map[aiger_strip(condition_lit)];
    // auto condition_satvar = aiger_sign(condition_lit) ? -node_satvar_map[condition_node.node] : node_satvar_map[condition_node.node];
    auto condition_satvar = get_satvar(condition_lit);
    unit(condition_satvar);

    // set phase hints: tell solver the preferred direction for constrained inputs
    for(int i = 0; i < condition_vec.size(); i++){
        unsigned cond_lit = aiger_strip(condition_vec[i]);
        // only set phase for primary inputs
        if(aiger_lit2var(cond_lit) <= model_->num_inputs){
            auto input_node = lit_node_map[cond_lit];
            auto satvar = node_satvar_map[input_node.node];
            if(satvar != 0){
                // phase(positive_lit) = prefer true; phase(negative_lit) = prefer false
                int phase_lit = aiger_sign(condition_vec[i]) ? -satvar : satvar;
                solver_->phase(phase_lit);
            }
        }
    }

    std::cout << "total create condition node number: " << condition_node_number << std::endl;

    auto enable = generate_initial_sim_hash_data(condition_lit, condition_vec);

    if(enable){
        std::cout << "can't generate condition sim data, so we set random sim data" << std::endl;
        for(int i = 0; i < model_ -> num_inputs; i ++){
        // get the lit from aiger model
        auto input_lit = model_ -> inputs[i].lit;

        // lit <-> node
        auto input_node = lit_node_map[input_lit];

        // sim hash
        auto sim_hash = get_random_uint64();
        node_simulation_hash_map[input_node.node] = sim_hash;

        auto sim_data = sim_hash;
        // sim data
        for(int sim_round = 0; sim_round < INITIAL_SIM_ROUND; sim_round++){
            // auto sim_data = get_random_uint64();
            node_simulation_data_map[input_node.node].push_back(sim_data);
        }
        
        }
    }
    
}

void CondEC::cec_ands_register(){
    for(int i = 0; i < model_->num_ands; i++){
        auto rhs0_lit = model_ -> ands[i].rhs0;
        auto rhs1_lit = model_ -> ands[i].rhs1;
        auto lhs_lit  = model_ -> ands[i].lhs;

        if(lhs_lit >= aiger_strip(model_->constraints[0].lit)) 
            break;

        // check node is created in condition register?
        bool condition_created = lit_has_node(lhs_lit);


        // structral hash
        auto structral_hash = create_structural_hash(model_ -> ands[i].rhs0, model_ -> ands[i].rhs1);
        // std::cout << "lit " << lhs_lit << " structural hash: " << structral_hash << std::endl;
        auto id1 = lit_node_map[aiger_strip(rhs0_lit)];
        auto id2 = lit_node_map[aiger_strip(rhs1_lit)];
        bool c1 = aiger_sign(rhs0_lit);
        bool c2 = aiger_sign(rhs1_lit);
        c1 = id1.neg ? !c1 : c1;
        c2 = id2.neg ? !c2 : c2;
        // normalize operand order for structural comparison
        if (id1.node > id2.node || (id1.node == id2.node && c1 > c2)) {
            std::swap(id1, id2);
            std::swap(c1, c2);
        }

        bool structural_hash_merge = false;
        if(structural_hash_nodevec_map.find(structral_hash) != structural_hash_nodevec_map.end()){  // check if same structural hash
            for(auto &other_node : structural_hash_nodevec_map[structral_hash]){
                auto other_lit = node_lit_map[other_node];
                if(aiger_lit2var(other_lit) <= model_->num_inputs){
                    continue;
                }
                aiger_and *other_and_gate = aiger_is_and(model_, other_lit);

                auto other_id1 = lit_node_map[aiger_strip(other_and_gate->rhs0)];
                auto other_id2 = lit_node_map[aiger_strip(other_and_gate->rhs1)];
                bool other_c1 = aiger_sign(other_and_gate->rhs0);
                bool other_c2 = aiger_sign(other_and_gate->rhs1);
                other_c1 = other_id1.neg ? !other_c1 : other_c1;
                other_c2 = other_id2.neg ? !other_c2 : other_c2;
                // normalize operand order for structural comparison
                if (other_id1.node > other_id2.node || (other_id1.node == other_id2.node && other_c1 > other_c2)) {
                    std::swap(other_id1, other_id2);
                    std::swap(other_c1, other_c2);
                }

                // check if same input (id1, id2, c1, c2)
                if((id1.node == other_id1.node) && (id2.node == other_id2.node) && (c1 == other_c1) && (c2 == other_c2)){
                    // node have equivalence node, not create, just map to old node
                    node_neg other_node_pn = {other_node,0};
                    lit_node_map[lhs_lit] = other_node_pn;
                    if(condition_created){
                        auto satvar = get_satvar(lhs_lit);
                        auto eq_satvar = node_satvar_map[other_node];
                        binary(satvar, -eq_satvar);
                        binary(-satvar, eq_satvar);
                    }

                    structural_hash_merge_num++;
                    structural_hash_merge = true;
                    break;  // jump out for
                }
            }

            if(structural_hash_merge){
                continue;   // jump to next and-gates
            }
        }


        // sim hash
        auto rhs0_sim_hash = get_simulation_hash(rhs0_lit);
        auto rhs1_sim_hash = get_simulation_hash(rhs1_lit);
        auto lhs_sim_hash = rhs0_sim_hash & rhs1_sim_hash;


        // sim data — cache node lookups outside the loop, pre-allocate vector
        int total_sim_rounds = INITIAL_SIM_ROUND + new_sim_data_num;
        std::vector<inputs_t> lhs_sim_data;
        lhs_sim_data.reserve(total_sim_rounds);

        auto rhs0_node_info = lit_node_map[aiger_strip(rhs0_lit)];
        auto rhs1_node_info = lit_node_map[aiger_strip(rhs1_lit)];
        bool rhs0_neg = aiger_sign(rhs0_lit) ^ rhs0_node_info.neg;
        bool rhs1_neg = aiger_sign(rhs1_lit) ^ rhs1_node_info.neg;
        auto &rhs0_sim_vec = node_simulation_data_map[rhs0_node_info.node];
        auto &rhs1_sim_vec = node_simulation_data_map[rhs1_node_info.node];

        for(int sim_round = 0; sim_round < total_sim_rounds; sim_round++){
            auto rhs0_sd = rhs0_neg ? ~rhs0_sim_vec[sim_round] : rhs0_sim_vec[sim_round];
            auto rhs1_sd = rhs1_neg ? ~rhs1_sim_vec[sim_round] : rhs1_sim_vec[sim_round];
            lhs_sim_data.push_back(rhs0_sd & rhs1_sd);
        }


        // find CEC possible equivalence node
        std::vector<unsigned> cec_candidate;
        if(simulation_hash_nodevec_map.find(lhs_sim_hash) != simulation_hash_nodevec_map.end()){    // check if same sim hash
            for(auto &other_node : simulation_hash_nodevec_map[lhs_sim_hash]){
                if(node_simulation_data_map[other_node] == lhs_sim_data){   // double check if same sim data
                    // push possible equivalence node
                    cec_candidate.push_back(other_node);
                }
            }
        }


        // CEC
        auto rhs0_satvar = get_satvar(rhs0_lit);
        auto rhs1_satvar = get_satvar(rhs1_lit);
        
        int satvar;
        if(condition_created){
            satvar = get_satvar(lhs_lit);
        }
        else{
            satvar = create_satvar();
        }

        bool merge = false;
        unsigned equivalence_node;
        if (!cec_candidate.empty()){
            merge = cec_checker(cec_candidate, equivalence_node, rhs0_satvar, rhs1_satvar, satvar);

            if(merge){
                // node have equivalence node, not create, just map to old node
                // new: pre_node = equivalence_node
                if(condition_created){
                    auto eq_satvar = node_satvar_map[equivalence_node];
                    binary(satvar, -eq_satvar);
                    binary(-satvar, eq_satvar);
                }
                node_neg equivalence_node_pn = {equivalence_node, 0};
                lit_node_map[lhs_lit] = equivalence_node_pn;
                cec_merge_num++;
                continue;
            }
        }
        assert(merge == false);

        // find CEC possible equivalence node (neg)
        std::vector<unsigned> cec_candidate_neg;
        auto lhs_sim_hash_neg = ~lhs_sim_hash;
        std::vector<uint64_t> lhs_sim_data_neg;
        lhs_sim_data_neg.reserve(lhs_sim_data.size());
        for (auto val : lhs_sim_data){
            lhs_sim_data_neg.push_back(~val);
        }

        if(simulation_hash_nodevec_map.find(lhs_sim_hash_neg) != simulation_hash_nodevec_map.end()){    // check if neg sim hash
            for(auto &other_node : simulation_hash_nodevec_map[lhs_sim_hash_neg]){
                if(node_simulation_data_map[other_node] == lhs_sim_data_neg){   // double check if neg sim data
                    // push possible equivalence node
                    cec_candidate_neg.push_back(other_node);
                }
            }
        }
        
        bool merge_neg = false;
        if (!cec_candidate_neg.empty()){
            merge_neg = cec_checker_neg(cec_candidate_neg, equivalence_node, rhs0_satvar, rhs1_satvar, satvar);

            if(merge_neg){
                if(condition_created){
                    auto eq_satvar = node_satvar_map[equivalence_node];
                    binary(satvar, eq_satvar);
                    binary(-satvar, -eq_satvar);
                }
                node_neg equivalence_node_pn = {equivalence_node, 1};
                lit_node_map[lhs_lit] = equivalence_node_pn; // -------------------------------------------------------------------------bug need to modify  lit -> -node
                cec_merge_num++;
                continue;
            }
        }
        assert(merge_neg == false); 

        // final handle
        // node haven't equivalence node, create new node and update map
        if(condition_created){
            // use previous node
            auto and_node = lit_node_map[lhs_lit];

            // lit <-> node, lit -> node already map
            node_lit_map[and_node.node] = lhs_lit;

            // node -> satvar already map
                
            // structural hash -> node
            structural_hash_nodevec_map[structral_hash].push_back(and_node.node);

            // node <-> simulation hash
            node_simulation_hash_map[and_node.node] = lhs_sim_hash;
            simulation_hash_nodevec_map[lhs_sim_hash].push_back(and_node.node);
    
            // node -> simulation data
            node_simulation_data_map[and_node.node] = lhs_sim_data;
        }
        else{
            // create new node
            auto and_node = create_new_node();

            // lit <-> node
            node_neg and_node_np = {and_node, 0};
            lit_node_map[lhs_lit] = and_node_np;
            node_lit_map[and_node] = lhs_lit;

            // node -> satvar
            node_satvar_map[and_node] = satvar; // first check node if have equivalence node, if node haven't equivalence node, use it

            // structural hash -> node
            structural_hash_nodevec_map[structral_hash].push_back(and_node);

            // node <-> simulation hash
            node_simulation_hash_map[and_node] = lhs_sim_hash;
            simulation_hash_nodevec_map[lhs_sim_hash].push_back(and_node);

            // node -> simulation data
            node_simulation_data_map[and_node] = lhs_sim_data;

            // add cnf
            create_andgate(satvar, rhs0_satvar, rhs1_satvar);
        }

        // if we have 64 sat solutions, update one round sim data
        if(new_input_pattern_vec.size() != 0){  // maybe new_input_pattern_vec not have at 0
            if(new_input_pattern_vec.at(0).size() >= 64*(new_sim_data_num + 1)){
                // create new input sim data from sat counterexamples
                create_new_simulation_data();
                // update sim data for all created node
                update_all_sim_data(lhs_lit);
            }
        }
    }   // end of for and-gates

}

int CondEC::cec_solve(){
    // print information about cec
    std::cout << "----------------FINAL----------------" << std::endl;
    std::cout << "sat              number: " << cec_sat_num << std::endl;
    std::cout << "unknow           number: " << cec_unknow_num << std::endl;
    std::cout << "unsat            number: " << cec_unsat_num << std::endl;
    std::cout << "structural merge number: " << structural_hash_merge_num << std::endl;
    std::cout << "cec merge        number: " << cec_merge_num << std::endl;
    std::cout << "new sim pattern  number: " << new_sim_data_num << std::endl;
    std::cout << "-------------------------------------" << std::endl;

    // Try old solver first with a conflict limit
    auto miter_out  = model_ -> outputs[0].lit;
    auto satvar = get_satvar(miter_out);
    unit(satvar);

    solver_->limit("conflicts", 100000);
    int res = solver_->solve();

    if (res == 0) {
        // Old solver inconclusive — rebuild a clean solver without dead clauses
        std::cout << "rebuilding fresh solver..." << std::endl;

        CaDiCaL::Solver *fresh = new CaDiCaL::Solver;
        fresh->set("walk", 0);
        fresh->set("condition", 0);

        std::unordered_map<unsigned, int> fresh_node_satvar;
        int fresh_var = 0;

        // constant-true variable
        ++fresh_var;
        fresh->resize(fresh_var);
        int const_true_var = fresh_var;
        fresh->clause(const_true_var);

        auto get_fresh_satvar = [&](unsigned lit) -> int {
            if (aiger_strip(lit) == 0) {
                return aiger_sign(lit) ? const_true_var : -const_true_var;
            }
            if (!lit_has_node(lit)) {
                ++fresh_var;
                fresh->resize(fresh_var);
                return aiger_sign(lit) ? -fresh_var : fresh_var;
            }
            auto node_info = lit_node_map[aiger_strip(lit)];
            bool neg = aiger_sign(lit) ^ node_info.neg;
            auto it = fresh_node_satvar.find(node_info.node);
            if (it == fresh_node_satvar.end()) {
                ++fresh_var;
                fresh->resize(fresh_var);
                it = fresh_node_satvar.emplace(node_info.node, fresh_var).first;
            }
            return neg ? -it->second : it->second;
        };

        for (int i = 0; i < model_->num_ands; i++) {
            int lhs_sv = get_fresh_satvar(model_->ands[i].lhs);
            int rhs0_sv = get_fresh_satvar(model_->ands[i].rhs0);
            int rhs1_sv = get_fresh_satvar(model_->ands[i].rhs1);
            fresh->clause(-lhs_sv, rhs0_sv);
            fresh->clause(-lhs_sv, rhs1_sv);
            fresh->clause(lhs_sv, -rhs0_sv, -rhs1_sv);
        }

        for (int i = 0; i < model_->num_constraints; i++) {
            fresh->clause(get_fresh_satvar(model_->constraints[i].lit));
        }

        fresh->clause(get_fresh_satvar(miter_out));
        std::cout << "fresh solver: " << fresh_var << " vars" << std::endl;

        res = fresh->solve();
        delete fresh;
    }

    if(res == 10)
        std::cout << "final sat result: SAT" << std::endl;
    else if(res == 20)
        std::cout << "final sat result: UNSAT" << std::endl;
    else
        std::cout << "final sat result: UNKNOW" << std::endl;

    return res;
}

