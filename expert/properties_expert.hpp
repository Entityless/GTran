/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Aaron Li (cjli@cse.cuhk.edu.hk)
*/

#ifndef EXPERT_PROPERTIES_EXPERT_HPP_
#define EXPERT_PROPERTIES_EXPERT_HPP_

#include <string>
#include <utility>
#include <vector>

#include "base/type.hpp"
#include "core/message.hpp"
#include "core/abstract_mailbox.hpp"
#include "expert/abstract_expert.hpp"
#include "expert/expert_validation_object.hpp"
#include "utils/tool.hpp"
#include "utils/timer.hpp"

class PropertiesExpert : public AbstractExpert {
 public:
    PropertiesExpert(int id,
            int machine_id,
            int num_thread,
            AbstractMailbox * mailbox,
            CoreAffinity* core_affinity) :
        AbstractExpert(id, core_affinity),
        machine_id_(machine_id),
        num_thread_(num_thread),
        mailbox_(mailbox),
        type_(EXPERT_T::PROPERTIES) {
        config_ = Config::GetInstance();
    }

    // inType, [key]+
    void process(const QueryPlan & qplan, Message & msg) {
        int tid = TidPoolManager::GetInstance()->GetTid(TID_TYPE::RDMA);

        Meta & m = msg.meta;
        Expert_Object expert_obj = qplan.experts[m.step];

        Element_T inType = (Element_T)Tool::value_t2int(expert_obj.params.at(0));
        vector<label_t> key_list;
        for (int cnt = 1; cnt < expert_obj.params.size(); cnt++) {
            key_list.push_back(static_cast<label_t>(Tool::value_t2int(expert_obj.params.at(cnt))));
        }

        if (qplan.trx_type != TRX_READONLY && config_->isolation_level == ISOLATION_LEVEL::SERIALIZABLE) {
            // Record Input Set
            for (auto & data_pair : msg.data) {
                v_obj.RecordInputSetValueT(qplan.trxid, expert_obj.index, inType, data_pair.second, m.step == 1 ? true : false);
            }
        }

        bool read_success = true;
        switch (inType) {
          case Element_T::VERTEX:
            read_success = get_properties_for_vertex(qplan, tid, key_list, msg.data);
            break;
          case Element_T::EDGE:
            read_success = get_properties_for_edge(qplan, tid, key_list, msg.data);
            break;
          default:
            cout << "Wrong in type" << endl;
        }

        vector<Message> msg_vec;
        if (read_success) {
            msg.CreateNextMsg(qplan.experts, msg.data, num_thread_, core_affinity_, msg_vec);
        } else {
            string abort_info = "Abort with [Processing][PropertiesExpert::process]";
            msg.CreateAbortMsg(qplan.experts, msg_vec, abort_info);
        }

        // Send Message
        for (auto& msg : msg_vec) {
            mailbox_->Send(tid, msg);
        }
    }

    bool valid(uint64_t TrxID, vector<Expert_Object*> & expert_list, const vector<rct_extract_data_t> & check_set) {
        for (auto expert_obj : expert_list) {
            CHECK(expert_obj->expert_type == EXPERT_T::PROPERTIES);
            vector<uint64_t> local_check_set;

            // Analysis params
            Element_T inType = (Element_T)Tool::value_t2int(expert_obj->params.at(0));
            set<int> plist;
            for (int cnt = 1; cnt < expert_obj->params.size(); cnt++) {
                plist.emplace(Tool::value_t2int(expert_obj->params.at(cnt)));
            }

            // Compare check_set and parameters
            for (auto & val : check_set) {
                if (plist.find(get<1>(val)) != plist.end() && get<2>(val) == inType) {
                    local_check_set.emplace_back(get<0>(val));
                }
            }

            if (local_check_set.size() != 0) {
                if (!v_obj.Validate(TrxID, expert_obj->index, local_check_set)) {
                    return false;
                }
            }
        }
        return true;
    }

    void clean_trx_data(uint64_t TrxID) { v_obj.DeleteInputSet(TrxID); }

 private:
    // Number of threads
    int num_thread_;
    int machine_id_;

    // Expert type
    EXPERT_T type_;

    // Pointer of mailbox
    AbstractMailbox * mailbox_;
    Config* config_;

    // Validation Store
    ExpertValidationObject v_obj;

    bool get_properties_for_vertex(const QueryPlan & qplan, int tid, const vector<label_t> & key_list,
                                   vector<pair<history_t, vector<value_t>>>& data) {
        for (auto & pair : data) {
            PushToRWRecord(qplan.trxid, pair.second.size(), true);
            vector<std::pair<uint64_t, string>> result;
            vector<value_t> newData;

            for (auto & value : pair.second) {
                vid_t v_id(Tool::value_t2int(value));

                vector<std::pair<label_t, value_t>> vp_kv_pair_list;
                READ_STAT read_status;
                if (key_list.empty()) {
                    read_status = data_storage_->GetAllVP(v_id, qplan.trxid, qplan.st, qplan.trx_type == TRX_READONLY, vp_kv_pair_list);
                } else {
                    read_status = data_storage_->GetVPByPKeyList(v_id, key_list, qplan.trxid, qplan.st, qplan.trx_type == TRX_READONLY, vp_kv_pair_list);
                }

                if (read_status == READ_STAT::ABORT) {
                    return false;
                } else if (read_status == READ_STAT::NOTFOUND) {
                    continue;
                }

                for (auto vp_kv_pair : vp_kv_pair_list) {
                    string keyStr;
                    data_storage_->GetNameFromIndex(Index_T::V_PROPERTY, vp_kv_pair.first, keyStr);

                    vpid_t vpid(v_id, vp_kv_pair.first);
                    string result_value = "{" + keyStr + ":" + vp_kv_pair.second.DebugString() + "}";
                    result.emplace_back(vpid.value(), result_value);
                }
            }

            Tool::vec_pair2value_t(result, newData);
            pair.second.swap(newData);
        }
        return true;
    }

    bool get_properties_for_edge(const QueryPlan & qplan, int tid, const vector<label_t> & key_list,
                                 vector<pair<history_t, vector<value_t>>>& data) {
        for (auto & pair : data) {
            PushToRWRecord(qplan.trxid, pair.second.size(), true);
            vector<std::pair<uint64_t, string>> result;
            vector<value_t> newData;

            for (auto & value : pair.second) {
                eid_t e_id;
                uint2eid_t(Tool::value_t2uint64_t(value), e_id);

                vector<std::pair<label_t, value_t>> ep_kv_pair_list;
                READ_STAT read_status;
                if (key_list.empty()) {
                    // read all properties
                    read_status = data_storage_->GetAllEP(e_id, qplan.trxid, qplan.st, qplan.trx_type == TRX_READONLY, ep_kv_pair_list);
                } else {
                    read_status = data_storage_->GetEPByPKeyList(e_id, key_list, qplan.trxid, qplan.st, qplan.trx_type == TRX_READONLY, ep_kv_pair_list);
                }

                if (read_status == READ_STAT::ABORT) {
                    return false;
                } else if (read_status == READ_STAT::NOTFOUND) {
                    continue;
                }

                for (auto ep_kv_pair : ep_kv_pair_list) {
                    string keyStr;
                    data_storage_->GetNameFromIndex(Index_T::E_PROPERTY, ep_kv_pair.first, keyStr);

                    epid_t epid(e_id, ep_kv_pair.first);
                    string result_value = "{" + keyStr + ":" + ep_kv_pair.second.DebugString() + "}";
                    result.emplace_back(epid.value(), result_value);
                }
            }

            Tool::vec_pair2value_t(result, newData);
            pair.second.swap(newData);
        }
        return true;
    }
};

#endif  // EXPERT_PROPERTIES_EXPERT_HPP_
