/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Aaron Li (cjli@cse.cuhk.edu.hk)
         Modified by Chenghuan Huang (chhuang@cse.cuhk.edu.hk)
*/
#pragma once
#include <limits.h>
#include <mkl_vsl.h>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <utility>

#include "actor/abstract_actor.hpp"
#include "actor/actor_cache.hpp"
#include "actor/actor_validation_object.hpp"
#include "core/result_collector.hpp"
#include "utils/tool.hpp"

#include "utils/mkl_util.hpp"

namespace BarrierData {
struct barrier_data_base {
    map<string, int> path_counter;
};
}  // namespace BarrierData

// Base class for barrier actors
template<class T = BarrierData::barrier_data_base>
class BarrierActorBase :  public AbstractActor {
    static_assert(std::is_base_of<BarrierData::barrier_data_base, T>::value,
                "T must derive from barrier_data_base");
    using BarrierDataTable = tbb::concurrent_hash_map<mkey_t, T, MkeyHashCompare>;

 public:
    BarrierActorBase(int id,
            CoreAffinity* core_affinity) :
        AbstractActor(id, core_affinity) {}

    void process(const QueryPlan & qplan, Message & msg) {
        int tid = TidMapper::GetInstance()->GetTid();

        // get msg info
        mkey_t key;
        string end_path;
        GetMsgInfo(msg, key, end_path);

        typename BarrierDataTable::accessor ac;
        data_table_.insert(ac, key);

        bool isReady = IsReady(ac, msg.meta, end_path);

        const vector<Actor_Object>& actors = qplan.actors;
        do_work(tid, actors, msg, ac, isReady);

        if (isReady) {
            data_table_.erase(ac);

            // don't need to send out msg when next actor is still barrier actor
            if (is_next_barrier(actors, msg.meta.step)) {
                // move to next actor
                msg.meta.step = actors[msg.meta.step].next_actor;
                if (actors[msg.meta.step].actor_type == ACTOR_T::COUNT) {
                    for (auto& p : msg.data) {
                        value_t v;
                        Tool::str2int(to_string(p.second.size()), v);
                        p.second.clear();
                        p.second.push_back(move(v));
                    }
                }
            }
        }
    }

 protected:
    virtual void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            typename BarrierDataTable::accessor& ac,
            bool isReady) = 0;

    // get labelled branch key if in branch
    static int get_branch_key(Meta & m) {
        // check if barrier actor in branch
        // run locally if true
        int branch_depth = m.branch_infos.size();
        int key = - 1;
        if (branch_depth != 0) {
            key = m.branch_infos[branch_depth - 1].key;
        }
        return key;
    }

    // get history value by given key
    static bool get_history_value(history_t& his, int history_key, value_t& val, bool erase_his = false) {
        if (history_key >= 0) {
            // find key from history
            auto his_itr = std::find_if(his.begin(), his.end(),
                [&history_key](const pair<int, value_t>& element)
                    { return element.first == history_key; });

            if (his_itr != his.end()) {
                val = move(his_itr->second);
                // some barrier actors will remove hisotry after branch key
                if (erase_his) {
                    his.erase(his_itr + 1, his.end());
                }
                return true;
            }
        }
        return false;
    }

    static int get_branch_value(history_t& his, int branch_key, bool erase_his = true) {
        value_t val;
        if (!get_history_value(his, branch_key, val, erase_his)) {
            return -1;
        }
        return Tool::value_t2int(val);
    }

    static inline bool is_next_barrier(const vector<Actor_Object>& actors, int step) {
        int next = actors[step].next_actor;
        return next < actors.size() && actors[next].IsBarrier();
    }

 private:
    // concurrent_hash_map, storing data for barrier processing
    BarrierDataTable data_table_;

    // Check if msg all collected
    static bool IsReady(typename BarrierDataTable::accessor& ac, Meta& m, string end_path) {
        map<string, int>& counter = ac->second.path_counter;
        string msg_path = m.msg_path;
        // check if all msg are collected
        while (msg_path != end_path) {
            int i = msg_path.find_last_of("\t");
            // "\t" should not be the the last char
            assert(i + 1 < msg_path.size());
            // get last number
            int num = atoi(msg_path.substr(i + 1).c_str());

            // check key
            if (counter.count(msg_path) != 1) {
                counter[msg_path] = 0;
            }

            // current branch is ready
            if ((++counter[msg_path]) == num) {
                // reset count to 0
                counter[msg_path] = 0;
                // remove last number
                msg_path = msg_path.substr(0, i == string::npos ? 0 : i);
            } else {
                return false;
            }
        }
        m.msg_path = end_path;
        return true;
    }

    // get msg info
    // key : mkey_t, identifier of msg
    // end_path: identifier of msg collection completed
    static void GetMsgInfo(Message& msg, mkey_t &key, string &end_path) {
        // init info
        uint64_t msg_id = 0;
        int index = 0;
        end_path = "";

        int branch_depth = msg.meta.branch_infos.size() - 1;
        if (branch_depth >= 0) {
            msg_id = msg.meta.branch_infos[branch_depth].msg_id;
            index = msg.meta.branch_infos[branch_depth].index;
            end_path = msg.meta.branch_infos[branch_depth].msg_path;
        }
        key = mkey_t(msg.meta.qid, msg_id, index);
    }
};

namespace BarrierData {
struct end_data : barrier_data_base{
    vector<value_t> result;
};
}  // namespace BarrierData

class EndActor : public BarrierActorBase<BarrierData::end_data> {
 public:
    EndActor(int id,
            int num_nodes,
            ResultCollector * rc,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::end_data>(id, core_affinity),
        num_nodes_(num_nodes),
        rc_(rc),
        mailbox_(mailbox) {}

 private:
    ResultCollector * rc_;
    AbstractMailbox * mailbox_;
    int num_nodes_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        #ifdef ACTOR_PROCESS_PRINT
        Node node = Node::StaticInstance();
        printf("%f, ACTOR = %s, %s, msg.meta.step = %d, node = %d, tid = %d\n",
                node.WtimeSinceStart(),
                "EndActor::do_work",
                actors[msg.meta.step].DebugString().c_str(),
                msg.meta.step,
                node.get_local_rank(),
                tid);
        #endif

        auto& data = ac->second.result;

        // move msg data to data table
        for (auto& pair : msg.data) {
            data.insert(data.end(),
                        std::make_move_iterator(pair.second.begin()),
                        std::make_move_iterator(pair.second.end()));
        }

        // all msg are collected
        if (isReady) {
            // insert data to result collector
            rc_->InsertResult(msg.meta.qid, data);

            vector<Message> vec;
            msg.CreateExitMsg(num_nodes_, vec);
            for (auto& m : vec) {
                mailbox_->Send(tid, m);
            }
        }
    }
};

namespace BarrierData {
struct agg_data : barrier_data_base {
    vector<value_t> agg_data;
    vector<pair<history_t, vector<value_t>>> msg_data;
};
}  // namespace BarrierData

class AggregateActor : public BarrierActorBase<BarrierData::agg_data> {
 public:
    AggregateActor(int id,
            int num_nodes,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::agg_data>(id, core_affinity),
        num_nodes_(num_nodes),
        num_thread_(num_thread),
        mailbox_(mailbox) {}

 private:
    int num_nodes_;
    int num_thread_;
    AbstractMailbox * mailbox_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& agg_data = ac->second.agg_data;
        auto& msg_data = ac->second.msg_data;

        // move msg data to data table
        for (auto& p : msg.data) {
            auto itr = find_if(msg_data.begin(), msg_data.end(),
                [&p](const pair<history_t, vector<value_t>>& element)
                    { return element.first == p.first; });

            if (itr == msg_data.end()) {
                itr = msg_data.insert(itr, make_pair(move(p.first), vector<value_t>()));
            }

            itr->second.insert(itr->second.end(), p.second.begin(), p.second.end());
            agg_data.insert(agg_data.end(),
                    std::make_move_iterator(p.second.begin()), std::make_move_iterator(p.second.end()));
        }

        // all msg are collected
        if (isReady) {
            const Actor_Object& actor = actors[msg.meta.step];
            assert(actor.params.size() == 1);
            int key = Tool::value_t2int(actor.params[0]);

            // insert to current node's storage
            data_storage_->InsertAggData(agg_t(msg.meta.qid, key), agg_data);

            vector<Message> v;
            // send aggregated data to other nodes
            msg.CreateFeedMsg(key, num_nodes_, agg_data, v);

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                // send input data and history to next actor
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
            }

            for (auto& m : v) {
                mailbox_->Send(tid, m);
            }
        }
    }
};

class CapActor : public BarrierActorBase<> {
 public:
    CapActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {}

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        // all msg are collected
        if (isReady) {
            const Actor_Object& actor = actors[msg.meta.step];
            vector<pair<history_t, vector<value_t>>> msg_data;
            msg_data.emplace_back(history_t(), vector<value_t>());

            // calculate max size of one value_t with empty history
            // max msg size - sizeof(data with one empty pair) - sizeof(empty value_t)
            size_t max_size = msg.max_data_size - MemSize(msg_data) - MemSize(value_t());

            assert(actor.params.size() % 2 == 0);

            // side-effect key list
            for (int i = 0; i < actor.params.size(); i+=2) {
                int se_key = Tool::value_t2int(actor.params.at(i));
                string se_string = Tool::value_t2string(actor.params.at(i+1));
                vector<value_t> vec;
                data_storage_->GetAggData(agg_t(msg.meta.qid, se_key), vec);

                string temp = se_string + ":[";
                for (auto& val : vec) {
                    temp += Tool::DebugString(val) + ", ";
                }
                // remove trailing ", "
                if (vec.size() > 0) {
                    temp.pop_back();
                    temp.pop_back();
                }
                temp += "]";

                while (true) {
                    value_t v;
                    // each value_t should have at most max_size
                    Tool::str2str(temp.substr(0, max_size), v);
                    msg_data[0].second.push_back(move(v));
                    if (temp.size() > max_size) {
                        temp = temp.substr(max_size);
                    } else {
                        break;
                    }
                }
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }
};

namespace BarrierData {
struct count_data : barrier_data_base {
    // int: assigned branch value by labelled branch step
    // pair:
    //  history_t: histroy of data
    //  int:       record num of incoming data
    unordered_map<int, pair<history_t, int>> counter_map;
};
}  // namespace BarrierData

class CountActor : public BarrierActorBase<BarrierData::count_data> {
 public:
    CountActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::count_data>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {}

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& counter_map = ac->second.counter_map;
        int branch_key = get_branch_key(msg.meta);

        // process msg data
        for (auto& p : msg.data) {
            int count = 0;
            if (p.second.size() != 0) {
                count = Tool::value_t2int(p.second[0]);
            }
            int branch_value = get_branch_value(p.first, branch_key);

            // get <history_t, int> pair by branch_value
            auto itr_cp = counter_map.find(branch_value);
            if (itr_cp == counter_map.end()) {
                itr_cp = counter_map.insert(itr_cp, {branch_value, {move(p.first), 0}});
            }
            itr_cp->second.second += count;
        }

        // all msg are collected
        if (isReady) {
            vector<pair<history_t, vector<value_t>>> msg_data;
            for (auto& p : counter_map) {
                value_t v;
                Tool::str2int(to_string(p.second.second), v);
                msg_data.emplace_back(move(p.second.first), vector<value_t>{move(v)});
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }
};

namespace BarrierData {
struct dedup_data : barrier_data_base {
    // int: assigned branch value by labelled branch step
     // vec: filtered data
     unordered_map<int, vector<pair<history_t, vector<value_t>>>> data_map;
    unordered_map<int, set<history_t>> dedup_his_map;    // for dedup by history
    unordered_map<int, set<value_t>> dedup_val_map;        // for dedup by value
};
}  // namespace BarrierData

class DedupActor : public BarrierActorBase<BarrierData::dedup_data> {
 public:
    DedupActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::dedup_data>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {}

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& data_map = ac->second.data_map;
        auto& dedup_his_map = ac->second.dedup_his_map;
        auto& dedup_val_map = ac->second.dedup_val_map;
        int branch_key = get_branch_key(msg.meta);

        // get actor params
        const Actor_Object& actor = actors[msg.meta.step];
        set<int> key_set;
        for (auto& param : actor.params) {
            key_set.insert(Tool::value_t2int(param));
        }

        // process msg data
        for (auto& p : msg.data) {
            int branch_value = get_branch_value(p.first, branch_key, false);

            // get data and history set under branch value
            auto& data_vec = data_map[branch_value];

            vector<pair<history_t, vector<value_t>>>::iterator itr_dp;
            if (data_vec.size() != 0) {
                if (data_vec[0].second.size() == 0) {
                    // clear useless history with empty data
                    data_vec.clear();
                    itr_dp = data_vec.end();
                } else {
                    // find if current history already added
                    itr_dp = find_if(data_vec.begin(), data_vec.end(),
                        [&p](const pair<history_t, vector<value_t>>& element){ return element.first == p.first;});
                }
            } else {
                itr_dp = data_vec.end();
            }

            if (itr_dp == data_vec.end()) {
                itr_dp = data_vec.insert(itr_dp, {p.first, vector<value_t>()});
            }

            if (key_set.size() > 0 && p.second.size() != 0) {
                auto& dedup_set = dedup_his_map[branch_value];
                history_t his;
                // dedup history
                // construct history with given key
                for (auto& val : p.first) {
                    if (key_set.find(val.first) != key_set.end()) {
                        his.push_back(move(val));
                    }
                }
                // insert constructed history and check if exists
                if (dedup_set.insert(move(his)).second) {
                    itr_dp->second.push_back(move(p.second[0]));
                }
            } else {
                auto& dedup_set = dedup_val_map[branch_value];
                // dedup value, should check on all values
                for (auto& val : p.second) {
                    // insert value to set and check if exists
                    if (dedup_set.insert(val).second) {
                        itr_dp->second.push_back(move(val));
                    }
                }
            }
        }

        // all msg are collected
        if (isReady) {
            vector<pair<history_t, vector<value_t>>> msg_data;
            for (auto& p : data_map) {
                msg_data.insert(msg_data.end(),
                                make_move_iterator(p.second.begin()),
                                make_move_iterator(p.second.end()));
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }
};

namespace BarrierData {
struct group_data : barrier_data_base {
    // int: assigned branch value by labelled branch step
     // pair:
     //        history_t:                 histroy of data
     //        map<string,value_t>:    record key and values of grouped data
     unordered_map<int, pair<history_t, map<string, vector<value_t>>>> data_map;
};
}  // namespace BarrierData

class GroupActor : public BarrierActorBase<BarrierData::group_data> {
 public:
    GroupActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::group_data>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {
        config_ = Config::GetInstance();
    }

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    Config* config_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& data_map = ac->second.data_map;
        int branch_key = get_branch_key(msg.meta);

        // get actor params
        const Actor_Object& actor = actors[msg.meta.step];
        assert(actor.params.size() == 2);
        int label_step = Tool::value_t2int(actor.params[1]);

        // process msg data
        for (auto& p : msg.data) {
            // Get projected key if any
            value_t k;
            string key;
            if (get_history_value(p.first, label_step, k)) {
                key = Tool::DebugString(k);
            }

            int branch_value = get_branch_value(p.first, branch_key);

            // get <history_t, map<string, vector<value_t>> pair by branch_value
            auto itr_data = data_map.find(branch_value);
            if (itr_data == data_map.end()) {
                itr_data = data_map.insert(itr_data, {branch_value, {move(p.first), map<string, vector<value_t>>()}});
            }
            auto& map_ = itr_data->second.second;

            for (auto& val : p.second) {
                if (label_step == -1) {
                    key = Tool::DebugString(val);
                }
                map_[key].push_back(move(val));
            }
        }

        // all msg are collected
        if (isReady) {
            bool isCount = Tool::value_t2int(actor.params[0]);
            vector<pair<history_t, vector<value_t>>> msg_data;

            for (auto& p : data_map) {
                // calculate max size of one map_string with given history
                // max msg size - sizeof(data_vec) - sizeof(current history) - sizeof(empty value_t)
                size_t max_size = msg.max_data_size - MemSize(msg_data) - MemSize(p.second.first) - MemSize(value_t());

                vector<value_t> vec_val;
                for (auto& item : p.second.second) {
                    string map_string;
                    // construct string
                    if (isCount) {
                        map_string = item.first + ":" + to_string(item.second.size());
                    } else {
                        map_string = item.first + ":[";
                        for (auto& v : item.second) {
                            map_string += Tool::DebugString(v) + ", ";
                        }
                        // remove trailing ", "
                        if (item.second.size() > 0) {
                            map_string.pop_back();
                            map_string.pop_back();
                        }
                        map_string += "]";
                    }

                    while (true) {
                        value_t v;
                        // each value_t should have at most max_size
                        Tool::str2str(map_string.substr(0, max_size), v);
                        vec_val.push_back(move(v));
                        if (map_string.size() > max_size) {
                            map_string = map_string.substr(max_size);
                        } else {
                            break;
                        }
                    }
                }
                msg_data.emplace_back(move(p.second.first), move(vec_val));
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }
};

namespace BarrierData {
struct order_data : barrier_data_base {
    // int: assigned branch value by labelled branch step
    // pair:
    //  history_t:                            histroy of data
    //  map<value_t, multiset<value_t>>:
    //      value_t:                          key for ordering
    //      multiset<value_t>:                store real data

    // for order with mapping
    unordered_map<int, pair<history_t, map<value_t, multiset<value_t>>>> data_map;
    // for order without mapping
    unordered_map<int, pair<history_t, multiset<value_t>>> data_set;
};
}  // namespace BarrierData

class OrderActor : public BarrierActorBase<BarrierData::order_data> {
 public:
    OrderActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::order_data>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {
        config_ = Config::GetInstance();
    }

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    Config* config_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& data_map = ac->second.data_map;
        auto& data_set = ac->second.data_set;
        int branch_key = get_branch_key(msg.meta);

        // get actor params
        const Actor_Object& actor = actors[msg.meta.step];
        assert(actor.params.size() == 2);
        int label_step = Tool::value_t2int(actor.params[0]);

        // process msg data
        for (auto& p : msg.data) {
            value_t key;
            get_history_value(p.first, label_step, key);
            int branch_value = get_branch_value(p.first, branch_key);
            if (label_step < 0) {
                // get <history_t, multiset<value_t>> pair by branch_value
                auto itr_data = data_set.find(branch_value);
                if (itr_data == data_set.end()) {
                    itr_data = data_set.insert(itr_data, {branch_value, {move(p.first), multiset<value_t>()}});
                }
                auto& set_ = itr_data->second.second;
                set_.insert(make_move_iterator(p.second.begin()), make_move_iterator(p.second.end()));
            } else {
                // get <history_t, map<value_t, multiset<value_t>>> pair by branch_value
                auto itr_data = data_map.find(branch_value);
                if (itr_data == data_map.end()) {
                    itr_data = data_map.insert(itr_data, {branch_value,
                                        {move(p.first), map<value_t, multiset<value_t>>()}});
                }
                auto& map_ = itr_data->second.second;

                map_[key].insert(make_move_iterator(p.second.begin()), make_move_iterator(p.second.end()));
            }
        }

        // all msg are collected
        if (isReady) {
            Order_T order = (Order_T)Tool::value_t2int(actor.params[1]);
            vector<pair<history_t, vector<value_t>>> msg_data;
            if (label_step < 0) {
                for (auto& p : data_set) {
                    vector<value_t> val_vec;
                    auto& set_ = p.second.second;
                    if (order == Order_T::INCR) {
                        val_vec.insert(val_vec.end(),
                                       make_move_iterator(set_.begin()),
                                       make_move_iterator(set_.end()));
                    } else {
                        val_vec.insert(val_vec.end(),
                                       make_move_iterator(set_.rbegin()),
                                       make_move_iterator(set_.rend()));
                    }
                    msg_data.emplace_back(move(p.second.first), move(val_vec));
                }
            } else {
                for (auto& p : data_map) {
                    vector<value_t> val_vec;
                    auto& m = p.second.second;
                    if (order == Order_T::INCR) {
                        for (auto itr = m.begin(); itr != m.end(); itr++) {
                            val_vec.insert(val_vec.end(),
                                           make_move_iterator(itr->second.begin()),
                                           make_move_iterator(itr->second.end()));
                        }
                    } else {
                        for (auto itr = m.rbegin(); itr != m.rend(); itr++) {
                            val_vec.insert(val_vec.end(),
                                           make_move_iterator(itr->second.rbegin()),
                                           make_move_iterator(itr->second.rend()));
                        }
                    }
                    msg_data.emplace_back(move(p.second.first), move(val_vec));
                }
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }
};

namespace BarrierData {
struct range_data : barrier_data_base{
    // int: assigned branch value by labelled branch step
    // pair:
    //        int: counter, record num of incoming data
    //        vec: record data in given range
    unordered_map<int, pair<int, vector<pair<history_t, vector<value_t>>>>> counter_map;
};
}  // namespace BarrierData

class RangeActor : public BarrierActorBase<BarrierData::range_data> {
 public:
    RangeActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::range_data>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {}

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& counter_map = ac->second.counter_map;
        int branch_key = get_branch_key(msg.meta);

        // get actor params
        const Actor_Object& actor = actors[msg.meta.step];
        assert(actor.params.size() == 2);
        int start = Tool::value_t2int(actor.params[0]);
        int end = Tool::value_t2int(actor.params[1]);
        if (end == -1) { end = INT_MAX; }

        // process msg data
        for (auto& p : msg.data) {
            int branch_value = get_branch_value(p.first, branch_key, false);

            // get <counter, vector<data_pair>> pair by branch_value
            auto itr_cp = counter_map.find(branch_value);
            if (itr_cp == counter_map.end()) {
                itr_cp = counter_map.insert(itr_cp, { branch_value,
                                        { 0, vector<pair<history_t, vector<value_t>>>()}});
            }
            auto& counter_pair = itr_cp->second;

            // get vector<data_pair>
            vector<pair<history_t, vector<value_t>>>::iterator itr_vec;
            // check vector<data_pair>
            if (counter_pair.second.size() != 0) {
                // skip when exceed limit
                if (counter_pair.first > end)
                    continue;

                if (counter_pair.second[0].second.size() == 0) {
                    // clear useless history with empty data
                    counter_pair.second.clear();
                    itr_vec = counter_pair.second.end();
                } else {
                    // find if current history already added
                    itr_vec = find_if(counter_pair.second.begin(), counter_pair.second.end(),
                        [&p](const pair<history_t, vector<value_t>>& element)
                            { return element.first == p.first; });
                }
            } else {
                itr_vec = counter_pair.second.end();
            }

            // insert new history
            if (itr_vec == counter_pair.second.end()) {
                itr_vec = counter_pair.second.insert(itr_vec, {move(p.first), vector<value_t>()});
            }

            for (auto& val : p.second) {
                if (counter_pair.first > end) {
                    break;
                }
                // insert value when start <= count <= end
                if (counter_pair.first >= start) {
                    itr_vec->second.push_back(move(val));
                }
                (counter_pair.first)++;
            }
        }

        // all msg are collected
        if (isReady) {
            vector<pair<history_t, vector<value_t>>> msg_data;
            for (auto& p : counter_map) {
                msg_data.insert(msg_data.end(),
                                make_move_iterator(p.second.second.begin()),
                                make_move_iterator(p.second.second.end()));
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }
};

class CoinActor : public BarrierActorBase<BarrierData::range_data> {
 public:
    CoinActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::range_data>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {}

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& counter_map = ac->second.counter_map;
        int branch_key = get_branch_key(msg.meta);

        // get actor params
        const Actor_Object& actor = actors[msg.meta.step];

        assert(actor.params.size() == 1);
        double rate = Tool::value_t2double(actor.params[0]);

        // process msg data
        for (auto& p : msg.data) {
            int branch_value = get_branch_value(p.first, branch_key, false);

            // get <counter, vector<data_pair>> pair by branch_value
            auto itr_cp = counter_map.find(branch_value);
            if (itr_cp == counter_map.end()) {
                itr_cp = counter_map.insert(itr_cp, { branch_value,
                        { 0, vector<pair<history_t, vector<value_t>>>() }});
            }
            auto& counter_pair = itr_cp->second;

            // get vector<data_pair>
            vector<pair<history_t, vector<value_t>>>::iterator itr_vec;
            // check vector<data_pair>
            if (counter_pair.second.size() != 0) {
                if (counter_pair.second[0].second.size() == 0) {
                    // clear useless history with empty data
                    counter_pair.second.clear();
                    itr_vec = counter_pair.second.end();
                } else {
                    // find if current history already added
                    itr_vec = find_if(counter_pair.second.begin(), counter_pair.second.end(),
                        [&p](const pair<history_t, vector<value_t>>& element)
                            { return element.first == p.first; });
                }
            } else {
                itr_vec = counter_pair.second.end();
            }

            // insert new history
            if (itr_vec == counter_pair.second.end()) {
                itr_vec = counter_pair.second.insert(itr_vec, {move(p.first), vector<value_t>()});
            }

            int sz = p.second.size();

            if (sz > 0) {
                float* tmp_rand_arr = new float[sz];

                MKLUtil::GetInstance()->UniformRNGF4(tmp_rand_arr, sz, 0.0, 1.0);

                for (int i = 0; i < sz; i++) {
                    if (tmp_rand_arr[i] < rate)
                        itr_vec->second.push_back(move(p.second[i]));
                }

                delete[] tmp_rand_arr;
            }
        }

        // all msg are collected
        if (isReady) {
            vector<pair<history_t, vector<value_t>>> msg_data;
            for (auto& p : counter_map) {
                msg_data.insert(msg_data.end(),
                                make_move_iterator(p.second.second.begin()),
                                make_move_iterator(p.second.second.end()));
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }
};

namespace BarrierData {
struct math_meta_t {
    int count;
    value_t value;
    history_t history;
};

struct math_data : barrier_data_base {
    // int: assigned branch value by labelled branch step
     // math_data_t: meta data for math
     unordered_map<int, math_meta_t> data_map;
};
}  // namespace BarrierData

class MathActor : public BarrierActorBase<BarrierData::math_data> {
 public:
    MathActor(int id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        BarrierActorBase<BarrierData::math_data>(id, core_affinity),
        num_thread_(num_thread),
        mailbox_(mailbox) {}

 private:
    int num_thread_;
    AbstractMailbox * mailbox_;

    void do_work(int tid,
            const vector<Actor_Object> & actors,
            Message & msg,
            BarrierDataTable::accessor& ac,
            bool isReady) {
        auto& data_map = ac->second.data_map;
        int branch_key = get_branch_key(msg.meta);

        // get actor params
        const Actor_Object& actor = actors[msg.meta.step];
        assert(actor.params.size() == 1);
        Math_T math_type = (Math_T)Tool::value_t2int(actor.params[0]);
        void (*op)(BarrierData::math_meta_t&, value_t&);
        switch (math_type) {
          case Math_T::SUM:
          case Math_T::MEAN:    op = sum; break;
          case Math_T::MAX:    op = max; break;
          case Math_T::MIN:    op = min; break;
          default:             cout << "Unexpected math type in MathActor" << endl;
        }

        // process msg data
        for (auto& p : msg.data) {
            int branch_value = get_branch_value(p.first, branch_key);

            // get math_data_t by branch_value
            auto itr_data = data_map.find(branch_value);
            if (itr_data == data_map.end()) {
                itr_data = data_map.insert(itr_data, {branch_value, BarrierData::math_meta_t()});
                itr_data->second.history = move(p.first);
                itr_data->second.count = 0;
            }

            for (auto& val : p.second) {
                op(itr_data->second, val);   // operate on new value
            }
        }

        // all msg are collected
        if (isReady) {
            bool isMean = math_type == Math_T::MEAN;
            vector<pair<history_t, vector<value_t>>> msg_data;
            for (auto& p : data_map) {
                BarrierData::math_meta_t& data = p.second;
                vector<value_t> val_vec;
                if (data.count > 0) {
                    // convert value to double
                    to_double(data, isMean);
                    val_vec.push_back(move(data.value));
                }
                msg_data.emplace_back(move(data.history), move(val_vec));
            }

            if (is_next_barrier(actors, msg.meta.step)) {
                msg.data = move(msg_data);
            } else {
                vector<Message> v;
                msg.CreateNextMsg(actors, msg_data, num_thread_, core_affinity_, v);
                for (auto& m : v) {
                    mailbox_->Send(tid, m);
                }
            }
        }
    }

    static void sum(BarrierData::math_meta_t& data, value_t& v) {
        data.count++;
        if (data.count == 1) {
            data.value = move(v);
            return;
        }
        value_t temp = data.value;
        data.value.content.clear();
        switch (v.type) {
          case 1:
            Tool::str2int(to_string(Tool::value_t2int(temp) + Tool::value_t2int(v)), data.value);
            break;
          case 2:
            Tool::str2double(to_string(Tool::value_t2double(temp) + Tool::value_t2double(v)), data.value);
            break;
        }
    }

    static void max(BarrierData::math_meta_t& data, value_t& v) {
        if (data.count == 0 || data.value < v) {
            data.value = move(v);
        }
        data.count++;
    }

    static void min(BarrierData::math_meta_t& data, value_t& v) {
        if (data.count == 0 || data.value > v) {
            data.value = move(v);
        }
        data.count++;
    }

    static void to_double(BarrierData::math_meta_t& data, bool isMean) {
        value_t temp = data.value;

        // divide value by count if isMean
        int count = isMean ? data.count : 1;

        data.value.content.clear();
        switch (data.value.type) {
          case 1:
            Tool::str2double(to_string(static_cast<double>(Tool::value_t2int(temp)) / count), data.value);
            break;
          case 2:
            Tool::str2double(to_string(Tool::value_t2double(temp) / count), data.value);
            break;
        }
    }
};
