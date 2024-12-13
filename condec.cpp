#include <iostream>
#include <vector>
#include <map>
#include <stack>
#include <random>

#include "condec.h"

extern "C" {
#include "aiger/aiger.h"
}

/*  create a hash for and-gates node (lhs)
    structural node hash = f(id1, id2, c1, c2)
*/
int CondEC::create_structural_hash(unsigned rsh0, unsigned rsh1){
    auto id1 = lit_node_map[aiger_strip(rsh0)];
    auto id2 = lit_node_map[aiger_strip(rsh1)];

    bool c1 = aiger_sign(rsh0); // true = neg, false = pos
    bool c2 = aiger_sign(rsh1); // true = neg, false = pos

    unsigned hash = 0;
    hash += id1 * 7937;
    hash += id2 * 2971;
    hash += c1  * 911;
    hash += c2  * 353;
    hash -= 2011;

    return (int)(hash);
}

inputs_t CondEC::get_random_uint64()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());

    uint64_t random_value = gen();

    return random_value;
}

sim_hash_t CondEC::get_sim_pattern_hash(unsigned index){
    if (index < sim_pattern_hash_vec.size())
        return sim_pattern_hash_vec.at(index);
    sim_hash_t ret = 0;
    
    for (unsigned bit = 0, mask = 1; bit < sim_pattern_hash_vec.size(); ++bit, mask <<= 1){
        auto exbit = index & mask; // you do need to shift right actually
        if (exbit)
          ret = ret ^ sim_pattern_hash_vec.at(bit);
      }
    if (ret == 0)
        ret = get_random_uint64();

    return ret;
}

//  get simulation hash or ~hash
sim_hash_t CondEC::get_simulation_hash(unsigned lit){
    bool neg = aiger_sign(lit);
    auto node = lit_node_map[aiger_strip(lit)];
    auto sim_hash = neg ? ~node_simulation_hash_map[node] : node_simulation_hash_map[node];
    return sim_hash;
}

//  get simulation data or ~data
inputs_t CondEC::get_simulation_data(unsigned lit, int sim_round){
    bool neg = aiger_sign(lit);
    auto node = lit_node_map[aiger_strip(lit)];
    auto sim_data = neg? ~node_simulation_data_map[node].at(sim_round) : node_simulation_data_map[node].at(sim_round);
    return sim_data;
}

// get satvar
int CondEC::get_satvar(unsigned int lit){
    auto node = lit_node_map[aiger_strip(lit)];
    auto satvar = aiger_sign(lit) ?  -node_satvar_map[node] : node_satvar_map[node];
    return satvar;
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
        node_simulation_data_map[input_node].push_back(new_sim_data);
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

        auto rhs0_sim_data = get_simulation_data(rhs0_lit, SIM_ROUND + new_sim_data_num - 1);
        auto rhs1_sim_data = get_simulation_data(rhs1_lit, SIM_ROUND + new_sim_data_num - 1);
        auto sim_data = rhs0_sim_data & rhs1_sim_data;

        auto and_node = lit_node_map[lhs_lit];
        if(node_simulation_data_map[and_node].size() == SIM_ROUND + new_sim_data_num - 1){
            node_simulation_data_map[and_node].push_back(sim_data);
        }
        
    }

}

// check equivalence node
bool CondEC::cec_checker(std::vector<unsigned> &cec_candidate, unsigned &equivalence_node, int rhs0_satvar, int rhs1_satvar, int lhs_satvar){
    for(auto &other_node : cec_candidate){
        // assume
        auto assumption = create_satvar();
        
        create_andgate(lhs_satvar, rhs0_satvar, rhs1_satvar, assumption); // clause OR assumption
        auto miter_i1 = node_satvar_map[other_node];
        auto miter_i2 = lhs_satvar;
        auto miter_o = create_satvar();
        create_miter(miter_o, miter_i1, miter_i2, assumption);  // clause OR assumption
        unit(miter_o, assumption);  // clause OR assumption
        solver_->assume(-assumption);   // make assumption = flase, enable clause of this function
        int res = solver_ -> solve();   // decision_limit 10000?, we hope to balance cec time and merge number

        if(res == CaDiCaL::SATISFIABLE){
            // add more sim round and sim data
            cec_sat_num++;
            if (new_input_pattern_vec.empty())
                new_input_pattern_vec.resize(model_->num_inputs);   // set input number vector
            for(int i = 0; i < model_ -> num_inputs; i ++){
                auto input_lit = model_ -> inputs[i].lit;
                auto input_node = lit_node_map[input_lit];
                auto input_satvar = node_satvar_map[input_node];

                int sat_assignment = solver_ -> val(input_satvar);
                bool sim_bit = (sat_assignment > 0) ? 1 : 0;
                new_input_pattern_vec.at(i).push_back(sim_bit);
            }
        }
        else if(res == CaDiCaL::UNKNOWN){
            // nothing to do
            cec_unknow_num++;
        }
        else if(res == CaDiCaL::UNSATISFIABLE){
            // merge equivalence node
            cec_unsat_num++;
            equivalence_node = other_node;
            unit(assumption);   // make assumption = true, disable clause of this function
            return true;    // merge
        }

        unit(assumption);   // make assumption = true, disable clause of this function
    }

    return false;   // no merge
}

void CondEC::cec_inputs_register(){
    for(int i = 0; i < model_ -> num_inputs; i ++){
        std::cout << "cec_ands_register (" << (i+1) << "/" << model_->num_inputs << ")" << std::endl;
        // get the lit from aiger model
        auto input_lit = model_ -> inputs[i].lit;

        // lit <-> node
        auto input_node = create_new_node();
        lit_node_map[input_lit] = input_node;
        node_lit_map[input_node] = input_lit;
        
        // node -> sat var
        auto satvar = create_satvar();
        node_satvar_map[input_node] = satvar;

        // structral hash -> node
        // structural_hash_nodevec_map[input_lit].push_back(input_node);
        structural_hash_nodevec_map[input_node].push_back(input_node);

        // node <-> sim hash
        auto sim_hash = get_sim_pattern_hash(i);
        node_simulation_hash_map[input_node] = sim_hash;
        simulation_hash_nodevec_map[sim_hash].push_back(input_node);
        std::cout << "input lit: " << input_lit << " <-> node: " << input_node << " <-> satvar: " << satvar << std::endl;
        std::cout << "input node " << input_node << " simulation hash: " << node_simulation_hash_map[input_node] << std::endl;

        // node -> sim data
        // for(int sim_round = 0; sim_round < SIM_ROUND; sim_round++){
        //     node_simulation_data_map[input_node].push_back(get_random_uint64());    // bug!!! reason: random sim data not sat condition
        //     std::cout << "sim round " << sim_round << ", simulation data: " << node_simulation_data_map[input_node].at(sim_round) << std::endl;
        // }

        // for test
        node_simulation_data_map[input_node].push_back(sim_hash);   // just 1 sim round

        std::cout << "----------------------------------------------------------------------------------------" << std::endl;
    }
}

void CondEC::cec_condition_register(unsigned int &condition_output){
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
        if(lit_node_map.find(aiger_strip(cur_lit)) != lit_node_map.end()){
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
            lit_node_map[cur_lit] = and_node;
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
    auto condition_node = lit_node_map[aiger_strip(condition_lit)];
    auto condition_satvar = aiger_sign(condition_lit) ? -node_satvar_map[condition_node] : node_satvar_map[condition_node];
    unit(condition_satvar);

    std::cout << "total create condition node number: " << condition_node_number << std::endl;
}

void CondEC::cec_ands_register(){
    for(int i = 0; i < model_->num_ands; i++){
        // get the lit from aiger model
        std::cout << "cec_ands_register (" << (i+1) << "/" << model_->num_ands << ")" << std::endl;
        auto rhs0_lit = model_ -> ands[i].rhs0;
        auto rhs1_lit = model_ -> ands[i].rhs1;
        auto lhs_lit  = model_ -> ands[i].lhs;


        // check node is created in condition register?
        bool condition_created = false;
        if(lit_node_map.find(aiger_strip(lhs_lit)) != lit_node_map.end()){
            std::cout << "this node is created in condition register" << std::endl;
            condition_created = true;
        }


        // structral hash
        auto structral_hash = create_structural_hash(model_ -> ands[i].rhs0, model_ -> ands[i].rhs1);
        // std::cout << "lit " << lhs_lit << " structural hash: " << structral_hash << std::endl;
        auto id1 = lit_node_map[aiger_strip(rhs0_lit)];
        auto id2 = lit_node_map[aiger_strip(rhs1_lit)];
        bool c1 = aiger_sign(rhs0_lit);
        bool c2 = aiger_sign(rhs1_lit);

        bool structural_hash_merge = false;
        if(structural_hash_nodevec_map.find(structral_hash) != structural_hash_nodevec_map.end()){  // check if same structural hash
            for(auto &other_node : structural_hash_nodevec_map[structral_hash]){
                auto other_lit = node_lit_map[other_node];
                aiger_and *other_and_gate = aiger_is_and(model_, other_lit);

                auto other_id1 = lit_node_map[aiger_strip(other_and_gate->rhs0)];
                auto other_id2 = lit_node_map[aiger_strip(other_and_gate->rhs1)];
                bool other_c1 = aiger_sign(other_and_gate->rhs0);
                bool other_c2 = aiger_sign(other_and_gate->rhs1);
                
                // check if same input (id1, id2, c1, c2)
                if((id1 == other_id1) && (id2 == other_id2) && (c1 == other_c1) && (c2 == other_c2)){
                    // node have equivalence node, not create, just map to old node
                    lit_node_map[lhs_lit] = other_node;

                    structural_hash_merge_num++;
                    structural_hash_merge = true;
                    std::cout << "[structural] lit " << lhs_lit << " merge!" << std::endl; 
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


        // sim data
        std::vector<inputs_t> lhs_sim_data;
        for(int sim_round = 0; sim_round < (SIM_ROUND + new_sim_data_num); sim_round++){
            auto rhs0_sim_data = get_simulation_data(rhs0_lit, sim_round);
            auto rhs1_sim_data = get_simulation_data(rhs1_lit, sim_round);
            auto sim_data = rhs0_sim_data & rhs1_sim_data;
            lhs_sim_data.push_back(sim_data);

            // std::cout << "and node " << lhs_lit << " sim round " << sim_round << " simulation data: " << sim_data << std::endl;
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

        // find CEC possible equivalence node (neg) 
            // ...


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
        }


        // final handle
        if(merge){
            // node have equivalence node, not create, just map to old node
            lit_node_map[lhs_lit] = equivalence_node;
            cec_merge_num++;
            std::cout << "[cec] lit " << lhs_lit << " merge!" << std::endl;
            std::cout << "sat: " << cec_sat_num  << ", unknow: " << cec_unknow_num << ", unsat: " << cec_unsat_num << std::endl;
            std::cout << "-------------------------------------" << std::endl;
            continue;
        }
        else{
            // node haven't equivalence node, create new node and update map
            if(condition_created){
                // use previous node
                auto and_node = lit_node_map[lhs_lit];

                // lit <-> node, lit -> node already map
                node_lit_map[and_node] = lhs_lit;

                // node -> satvar already map
                
                // structural hash -> node
                structural_hash_nodevec_map[structral_hash].push_back(and_node);

                // node <-> simulation hash
                node_simulation_hash_map[and_node] = lhs_sim_hash;
                simulation_hash_nodevec_map[lhs_sim_hash].push_back(and_node);

                // node -> simulation data
                node_simulation_data_map[and_node] = lhs_sim_data;

                // cnf already add in cec_condition_register    
                // auto satvar = node_satvar_map[and_node];          
                // create_andgate(satvar, rhs0_satvar, rhs1_satvar); 
            }
            else{
                // create new node
                auto and_node = create_new_node();

                // lit <-> node
                lit_node_map[lhs_lit] = and_node;
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
                    std::cout << "new_input_pattern_vec.at(0).size(): " << new_input_pattern_vec.at(0).size() << std::endl; 
                    // creater new input sim data from picosat sat
                    create_new_simulation_data();
                    // update sim data for all created node
                    update_all_sim_data(lhs_lit);
                }
            }

        }   // end of final handle

        std::cout << "sat: " << cec_sat_num  << ", unknow: " << cec_unknow_num << ", unsat: " << cec_unsat_num << std::endl;
        std::cout << "-------------------------------------" << std::endl;
    }   // end of for and-gates


    // print infomation about cec
    std::cout << "----------------FINAL----------------" << std::endl;
    std::cout << "sat              number: " << cec_sat_num << std::endl;
    std::cout << "unknow           number: " << cec_unknow_num << std::endl;
    std::cout << "unsat            number: " << cec_unsat_num << std::endl;
    std::cout << "structural merge number: " << structural_hash_merge_num << std::endl;
    std::cout << "cec merge        number: " << cec_merge_num << std::endl;
    std::cout << "new sim pattern  number: " << new_input_pattern_vec.at(0).size() << std::endl;
    std::cout << "-------------------------------------" << std::endl;

    // final sat for output
    auto lhs_lit  = model_ -> outputs[0].lit;
    auto satvar =get_satvar(lhs_lit);
    unit(satvar);
    int res = solver_ -> solve();    //return 10 = sat, 20 = unsat, 0 = unknow
    if(res == 10)
        std::cout << "final sat result: SAT" << std::endl;
    else if(res == 20)
        std::cout << "final sat result: UNSAT" << std::endl;
    else if(res == 0)
        std::cout << "final sat result: UNKNOW" << std::endl;
    std::cout << "-------------------------------------" << std::endl;

}


