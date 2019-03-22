/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Nick Fang (jcfang6@cse.cuhk.edu.hk)
*/
#include <utility>
#include "core/exec_plan.hpp"

ibinstream& operator<<(ibinstream& m, const QueryPlan& plan) {
    m << plan.query_index;
    m << plan.actors;
    m << plan.trx_type;
    m << plan.trxid;
    m << plan.st;
    return m;
}

obinstream& operator>>(obinstream& m, QueryPlan& plan) {
    m >> plan.query_index;
    m >> plan.actors;
    m >> plan.trx_type;
    m >> plan.trxid;
    m >> plan.st;
    return m;
}

void TrxPlan::RegPlaceHolder(uint8_t src_index, uint8_t dst_index, int actor_index, int param_index) {
    // Record the position of placeholder
    // When src_index is finished, results will be inserted into recorded positions.
    place_holder_[src_index].emplace_back(dst_index, actor_index, param_index);
    RegDependency(src_index, dst_index);
}

void TrxPlan::RegDependency(uint8_t src_index, uint8_t dst_index) {
    auto ret = topo_[src_index].insert(dst_index);

    // If not duplicated, increase count
    if (ret.second) {
        deps_count_[dst_index] ++;
    }
}

void TrxPlan::FillResult(uint8_t query_index, vector<value_t>& vec) {
    // Find placeholders that depend on results of query_index_
    for (position_t& pos : place_holder_[query_index]) {
        Actor_Object& actor = query_plans_[pos.query].actors[pos.actor];
        if (pos.param == -1) {
            // insert to the end of params
            pos.param = actor.params.size();
        }
        switch (actor.actor_type) {
          case ACTOR_T::INIT:
          case ACTOR_T::ADDE:
            actor.params.insert(actor.params.begin() + pos.param, vec.begin(), vec.end());
            break;
          default:
            value_t result;
            if (vec.size() == 1) {
                result = vec[0];
            } else {
                Tool::vec2value_t(vec, result);
            }
            actor.params[pos.param] = result;
            break;
        }
    }

    for (uint8_t index : topo_[query_index]) {
        deps_count_[index] --;
    }

    // Append result set
    // Add query header info if not parser error
    if (query_index != -1) {
        value_t v;
        string header = "Query " + to_string(query_index + 1) + ": ";
        Tool::str2str(header, v);
        results_[query_index].push_back(v);
    }

    results_[query_index].insert(results_[query_index].end(),
                                make_move_iterator(vec.begin()),
                                make_move_iterator(vec.end()));
    received_++;
}

bool TrxPlan::NextQueries(vector<QueryPlan>& plans) {
    // End of transaction
    if (received_ == query_plans_.size()) {
        return false;
    }

    for (auto itr = deps_count_.begin(); itr != deps_count_.end();) {
        // Send out queries whose dependency count = 0
        if(itr->second == 0) {
            // Set transaction info
            QueryPlan& plan = query_plans_[itr->first];
            plan.query_index = itr->first;
            plan.trxid = trxid;
            plan.st = st_;
            plan.trx_type = trx_type_;
            plans.push_back(move(plan));

            // erase to reduce search space
            itr = deps_count_.erase(itr);
        } else {
            itr++;
        }
    }
    return true;
}

void TrxPlan::GetResult(vector<value_t>& vec) {
    // Append query results in increasing order
    for (auto itr = results_.begin(); itr != results_.end(); itr ++) {
        vec.insert(vec.end(), make_move_iterator(itr->second.begin()), make_move_iterator(itr->second.end()));
    }
}
