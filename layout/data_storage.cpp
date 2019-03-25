/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Chenghuan Huang (chhuang@cse.cuhk.edu.hk)
*/

#include "layout/data_storage.hpp"

template<class MVCC> OffsetConcurrentMemPool<MVCC>* MVCCList<MVCC>::pool_ptr_ = nullptr;
template<class PropertyRow> OffsetConcurrentMemPool<PropertyRow>* PropertyRowList<PropertyRow>::pool_ptr_ = nullptr;
template<class PropertyRow> MVCCValueStore* PropertyRowList<PropertyRow>::value_storage_ptr_ = nullptr;
OffsetConcurrentMemPool<VertexEdgeRow>* TopologyRowList::pool_ptr_ = nullptr;

void DataStorage::Init() {
    node_ = Node::StaticInstance();
    config_ = Config::GetInstance();
    id_mapper_ = SimpleIdMapper::GetInstance();
    snapshot_manager_ = MPISnapshotManager::GetInstance();
    worker_rank_ = node_.get_local_rank();
    worker_size_ = node_.get_local_size();

    node_.Rank0PrintfWithWorkerBarrier(
                      "VE_ROW_ITEM_COUNT = %d, sizeof(EdgeHeader) = %d, sizeof(VertexEdgeRow) = %d\n",
                       VE_ROW_ITEM_COUNT, sizeof(EdgeHeader), sizeof(VertexEdgeRow));
    node_.Rank0PrintfWithWorkerBarrier(
                       "VP_ROW_ITEM_COUNT = %d, sizeof(VPHeader) = %d, sizeof(VertexPropertyRow) = %d\n",
                       VP_ROW_ITEM_COUNT, sizeof(VPHeader), sizeof(VertexPropertyRow));
    node_.Rank0PrintfWithWorkerBarrier(
                       "EP_ROW_ITEM_COUNT = %d, sizeof(EPHeader) = %d, sizeof(EdgePropertyRow) = %d\n",
                       EP_ROW_ITEM_COUNT, sizeof(EPHeader), sizeof(EdgePropertyRow));
    node_.Rank0PrintfWithWorkerBarrier(
                       "sizeof(PropertyMVCC) = %d, sizeof(VertexMVCC) = %d, sizeof(EdgeMVCC) = %d\n",
                       sizeof(PropertyMVCC), sizeof(VertexMVCC), sizeof(EdgeMVCC));

    CreateContainer();

    snapshot_manager_->SetRootPath(config_->SNAPSHOT_PATH);
    snapshot_manager_->AppendConfig("HDFS_INDEX_PATH", config_->HDFS_INDEX_PATH);
    snapshot_manager_->AppendConfig("HDFS_VTX_SUBFOLDER", config_->HDFS_VTX_SUBFOLDER);
    snapshot_manager_->AppendConfig("HDFS_VP_SUBFOLDER", config_->HDFS_VP_SUBFOLDER);
    snapshot_manager_->AppendConfig("HDFS_EP_SUBFOLDER", config_->HDFS_EP_SUBFOLDER);
    snapshot_manager_->SetComm(node_.local_comm);
    snapshot_manager_->ConfirmConfig();

    hdfs_data_loader_ = HDFSDataLoader::GetInstance();
    hdfs_data_loader_->LoadData();
    FillContainer();
    PrintLoadedData();
    PropertyMVCCTest();
    hdfs_data_loader_->FreeMemory();

    vid_to_assign_divided_ = worker_rank_;

    node_.Rank0PrintfWithWorkerBarrier("DataStorage::Init() all finished\n");
}

void DataStorage::CreateContainer() {
    ve_row_pool_ = OffsetConcurrentMemPool<VertexEdgeRow>::GetInstance(nullptr, config_->global_ve_row_pool_size);
    vp_row_pool_ = OffsetConcurrentMemPool<VertexPropertyRow>::GetInstance(nullptr, config_->global_vp_row_pool_size);
    ep_row_pool_ = OffsetConcurrentMemPool<EdgePropertyRow>::GetInstance(nullptr, config_->global_ep_row_pool_size);
    property_mvcc_pool_ = OffsetConcurrentMemPool<PropertyMVCC>::GetInstance(
                          nullptr, config_->global_property_mvcc_pool_size);
    vertex_mvcc_pool_ = OffsetConcurrentMemPool<VertexMVCC>::GetInstance(nullptr, config_->global_topo_mvcc_pool_size);
    edge_mvcc_pool_ = OffsetConcurrentMemPool<EdgeMVCC>::GetInstance(nullptr, config_->global_topo_mvcc_pool_size);

    MVCCList<PropertyMVCC>::SetGlobalMemoryPool(property_mvcc_pool_);
    MVCCList<VertexMVCC>::SetGlobalMemoryPool(vertex_mvcc_pool_);
    MVCCList<EdgeMVCC>::SetGlobalMemoryPool(edge_mvcc_pool_);
    PropertyRowList<EdgePropertyRow>::SetGlobalMemoryPool(ep_row_pool_);
    PropertyRowList<VertexPropertyRow>::SetGlobalMemoryPool(vp_row_pool_);
    TopologyRowList::SetGlobalMemoryPool(ve_row_pool_);

    uint64_t ep_sz = GiB2B(config_->global_edge_property_kv_sz_gb);
    uint64_t vp_sz = GiB2B(config_->global_vertex_property_kv_sz_gb);
    ep_store_ = new MVCCValueStore(nullptr, ep_sz / MemItemSize);
    vp_store_ = new MVCCValueStore(nullptr, vp_sz / MemItemSize);
    PropertyRowList<EdgePropertyRow>::SetGlobalKVS(ep_store_);
    PropertyRowList<VertexPropertyRow>::SetGlobalKVS(vp_store_);
}

void DataStorage::FillContainer() {
    indexes_ = hdfs_data_loader_->indexes_;

    // access elements in HDFSDataLoader
    hash_map<uint32_t, TMPVertex*>& v_map = hdfs_data_loader_->vtx_part_map_;
    hash_map<uint64_t, TMPEdge*>& e_map = hdfs_data_loader_->edge_part_map_;

    for (auto vtx : hdfs_data_loader_->shuffled_vtx_) {
        VertexAccessor v_accessor;
        vertex_map_.insert(v_accessor, vtx.id.value());

        if (vid_to_assign_divided_ <= vtx.id.value())
            vid_to_assign_divided_ = vtx.id.value() + worker_size_;

        v_accessor->second.label = vtx.label;
        v_accessor->second.vp_row_list = new PropertyRowList<VertexPropertyRow>;
        v_accessor->second.ve_row_list = new TopologyRowList;

        v_accessor->second.vp_row_list->Init();
        v_accessor->second.ve_row_list->Init();

        v_accessor->second.mvcc_list = new MVCCList<VertexMVCC>;
        v_accessor->second.mvcc_list->AppendInitialVersion()[0] = true;

        for (auto in_nb : vtx.in_nbs) {
            eid_t eid = eid_t(vtx.id.vid, in_nb.vid);

            // "false" means that is_out = false
            v_accessor->second.ve_row_list->InsertInitialElement(false, in_nb, e_map[eid.value()]->label, nullptr);
        }

        for (auto out_nb : vtx.out_nbs) {
            eid_t eid = eid_t(out_nb.vid, vtx.id.vid);

            EdgeAccessor e_accessor;
            edge_map_.insert(e_accessor, eid.value());
            auto* ep_row_list = new PropertyRowList<EdgePropertyRow>;
            ep_row_list->Init();

            // "true" means that is_out = true
            auto* mvcc_list = v_accessor->second.ve_row_list
                              ->InsertInitialElement(true, out_nb, e_map[eid.value()]->label, ep_row_list);
            e_accessor->second = mvcc_list;
        }

        for (int i = 0; i < vtx.vp_label_list.size(); i++) {
            v_accessor->second.vp_row_list->InsertInitialElement(vpid_t(vtx.id, vtx.vp_label_list[i]),
                                                                 vtx.vp_value_list[i]);
        }
    }
    vid_to_assign_divided_ = (vid_to_assign_divided_ - worker_rank_) / worker_size_;

    node_.Rank0PrintfWithWorkerBarrier("DataStorage::FillContainer() load vtx finished\n");

    for (auto edge : hdfs_data_loader_->shuffled_edge_) {
        EdgeConstAccessor e_accessor;
        edge_map_.find(e_accessor, edge.id.value());

        auto edge_item = e_accessor->second->GetInitVersion()->GetValue();

        for (int i = 0; i < edge.ep_label_list.size(); i++) {
            edge_item.ep_row_list->InsertInitialElement(epid_t(edge.id, edge.ep_label_list[i]), edge.ep_value_list[i]);
        }
    }

    #ifdef OFFSET_MEMORY_POOL_DEBUG
    node_.LocalSequentialDebugPrint("ve_row_pool_: " + ve_row_pool_->UsageString());
    node_.LocalSequentialDebugPrint("vp_row_pool_: " + vp_row_pool_->UsageString());
    node_.LocalSequentialDebugPrint("ep_row_pool_: " + ep_row_pool_->UsageString());
    node_.LocalSequentialDebugPrint("property_mvcc_pool_: " + property_mvcc_pool_->UsageString());
    node_.LocalSequentialDebugPrint("vertex_mvcc_pool_: " + vertex_mvcc_pool_->UsageString());
    node_.LocalSequentialDebugPrint("edge_mvcc_pool_: " + edge_mvcc_pool_->UsageString());
    #endif  // OFFSET_MEMORY_POOL_DEBUG
    #ifdef MVCC_VALUE_STORE_DEBUG
    node_.LocalSequentialDebugPrint("vp_store_: " + vp_store_->UsageString());
    node_.LocalSequentialDebugPrint("ep_store_: " + ep_store_->UsageString());
    #endif  // MVCC_VALUE_STORE_DEBUG

    node_.Rank0PrintfWithWorkerBarrier("DataStorage::FillContainer() finished\n");
}

void DataStorage::GetVP(const vpid_t& pid, const uint64_t& trx_id, const uint64_t& begin_time, value_t& ret) {
    vid_t vid = pid.vid;

    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid.value());

    v_accessor->second.vp_row_list->ReadProperty(pid, trx_id, begin_time, ret);
}

void DataStorage::GetVP(const vid_t& vid, const uint64_t& trx_id, const uint64_t& begin_time,
                        vector<pair<label_t, value_t>>& ret) {
    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid.value());

    v_accessor->second.vp_row_list->ReadAllProperty(trx_id, begin_time, ret);
}

void DataStorage::GetVPidList(const vid_t& vid, const uint64_t& trx_id, const uint64_t& begin_time,
                              vector<vpid_t>& ret) {
    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid.value());

    v_accessor->second.vp_row_list->ReadPidList(trx_id, begin_time, ret);
}

label_t DataStorage::GetVL(const vid_t& vid, const uint64_t& trx_id, const uint64_t& begin_time) {
    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid.value());

    return v_accessor->second.label;
}

void DataStorage::GetEP(const epid_t& pid, const uint64_t& trx_id, const uint64_t& begin_time, value_t& ret) {
    eid_t eid = eid_t(pid.in_vid, pid.out_vid);

    EdgeConstAccessor e_accessor;
    edge_map_.find(e_accessor, eid.value());

    auto edge_item = e_accessor->second->GetVisibleVersion(trx_id, begin_time)->GetValue();

    if (edge_item.Exist())
        edge_item.ep_row_list->ReadProperty(pid, trx_id, begin_time, ret);
}

void DataStorage::GetEP(const eid_t& eid, const uint64_t& trx_id, const uint64_t& begin_time,
                        vector<pair<label_t, value_t>>& ret) {
    EdgeConstAccessor e_accessor;
    edge_map_.find(e_accessor, eid.value());

    auto edge_item = e_accessor->second->GetVisibleVersion(trx_id, begin_time)->GetValue();
    if (edge_item.Exist())
        edge_item.ep_row_list->ReadAllProperty(trx_id, begin_time, ret);
}

label_t DataStorage::GetEL(const eid_t& eid, const uint64_t& trx_id, const uint64_t& begin_time) {
    EdgeConstAccessor e_accessor;
    edge_map_.find(e_accessor, eid.value());

    auto edge_item = e_accessor->second->GetVisibleVersion(trx_id, begin_time)->GetValue();

    return edge_item.label;
}

void DataStorage::GetEPidList(const eid_t& eid, const uint64_t& trx_id, const uint64_t& begin_time,
                              vector<epid_t>& ret) {
    EdgeConstAccessor e_accessor;
    edge_map_.find(e_accessor, eid.value());

    auto edge_item = e_accessor->second->GetVisibleVersion(trx_id, begin_time)->GetValue();
    if (edge_item.Exist())
        edge_item.ep_row_list->ReadPidList(trx_id, begin_time, ret);
}

void DataStorage::GetConnectedVertexList(const vid_t& vid, const label_t& edge_label, const Direction_T& direction,
                                         const uint64_t& trx_id, const uint64_t& begin_time, vector<vid_t>& ret) {
    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid.value());

    v_accessor->second.ve_row_list->ReadConnectedVertex(direction, edge_label, trx_id, begin_time, ret);
}

void DataStorage::GetConnectedEdgeList(const vid_t& vid, const label_t& edge_label, const Direction_T& direction,
                                       const uint64_t& trx_id, const uint64_t& begin_time, vector<eid_t>& ret) {
    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid.value());

    v_accessor->second.ve_row_list->ReadConnectedEdge(vid, direction, edge_label, trx_id, begin_time, ret);
}

void DataStorage::GetAllVertex(const uint64_t& trx_id, const uint64_t& begin_time, vector<vid_t>& ret) {
    for (auto v_pair = vertex_map_.begin(); v_pair != vertex_map_.end(); v_pair++) {
        auto& v_item = v_pair->second;

        if (v_item.mvcc_list->GetVisibleVersion(trx_id, begin_time)->GetValue())
            ret.emplace_back(vid_t(v_pair->first));
    }
}

void DataStorage::GetAllEdge(const uint64_t& trx_id, const uint64_t& begin_time, vector<eid_t>& ret) {
    // TODO(entityless): Simplify the code by editing eid_t::value()
    for (auto e_pair = edge_map_.begin(); e_pair != edge_map_.end(); e_pair++) {
        if (e_pair->second->GetVisibleVersion(trx_id, begin_time)->GetValue().Exist()) {
            uint64_t eid_fetched = e_pair->first;
            eid_t* tmp_eid_p = reinterpret_cast<eid_t*>(&eid_fetched);
            ret.emplace_back(eid_t(tmp_eid_p->out_v, tmp_eid_p->in_v));
        }
    }
}

void DataStorage::GetNameFromIndex(const Index_T& type, const label_t& id, string& str) {
    unordered_map<label_t, string>::const_iterator itr;

    switch (type) {
        case Index_T::E_LABEL:
            itr = indexes_->el2str.find(id);
            if (itr == indexes_->el2str.end())
                return;
            else
                str = itr->second;
            break;
        case Index_T::E_PROPERTY:
            itr = indexes_->epk2str.find(id);
            if (itr == indexes_->epk2str.end())
                return;
            else
                str = itr->second;
            break;
        case Index_T::V_LABEL:
            itr = indexes_->vl2str.find(id);
            if (itr == indexes_->vl2str.end())
                return;
            else
                str = itr->second;
            break;
        case Index_T::V_PROPERTY:
            itr = indexes_->vpk2str.find(id);
            if (itr == indexes_->vpk2str.end())
                return;
            else
                str = itr->second;
            break;
        default:
            return;
    }
}

void DataStorage::GetDepReadTrxList(uint64_t trxID, vector<uint64_t> & homoTrxIDList, vector<uint64_t> & heteroTrxIDList) {
    dep_trx_const_accessor c_accessor;

    if (dep_trx_map.find(c_accessor, trxID)) {
        homoTrxIDList = c_accessor->second.homo_trx_list;
        heteroTrxIDList =  c_accessor->second.hetero_trx_list;
    }
}

void DataStorage::CleanDepReadTrxList(uint64_t trxID) {
    dep_trx_accessor accessor;

    if (dep_trx_map.find(accessor, trxID)) {
        dep_trx_map.erase(accessor);
    }
}

void DataStorage::PrintLoadedData() {
    node_.LocalSequentialStart();

    if (hdfs_data_loader_->shuffled_vtx_.size() < 20 && hdfs_data_loader_->shuffled_edge_.size() < 40) {
        for (auto vtx : hdfs_data_loader_->shuffled_vtx_) {
            TMPVertex tmp_vtx;
            tmp_vtx.id = vtx.id;

            tmp_vtx.label = GetVL(vtx.id, 0x8000000000000001, 1);

            // vector<pair<label_t, value_t>> properties;
            // GetVP(vtx.id, 0x8000000000000001, 1, properties);
            // for (auto p : properties) {
            //     tmp_vtx.vp_label_list.push_back(p.first);
            //     tmp_vtx.vp_value_list.push_back(p.second);
            // }

            // for (int i = 0; i < vtx.vp_label_list.size(); i++) {
            //     tmp_vtx.vp_label_list.push_back(vtx.vp_label_list[i]);
            //     value_t val;
            //     GetVP(vpid_t(vtx.id, vtx.vp_label_list[i]), 0x8000000000000001, 1, val);
            //     tmp_vtx.vp_value_list.push_back(val);
            // }

            vector<vpid_t> vpids;
            GetVPidList(vtx.id, 0x8000000000000001, 1, vpids);
            for (int i = 0; i < vpids.size(); i++) {
                tmp_vtx.vp_label_list.push_back(vpids[i].pid);
                value_t val;
                GetVP(vpids[i], 0x8000000000000001, 1, val);
                tmp_vtx.vp_value_list.push_back(val);
            }

            // vector<vid_t> in_nbs;
            // GetConnectedVertexList(vtx.id, 0, IN, 0x8000000000000001, 1, in_nbs);
            // for (auto in_nb : in_nbs) {
            //     tmp_vtx.in_nbs.push_back(in_nb);
            // }

            // vector<eid_t> in_es;
            // GetConnectedEdgeList(vtx.id, 0, IN, 0x8000000000000001, 1, in_es);
            // for (auto in_e : in_es) {
            //     tmp_vtx.in_nbs.push_back(in_e.out_v);
            // }

            // vector<vid_t> out_nbs;
            // GetConnectedVertexList(vtx.id, 0, OUT, 0x8000000000000001, 1, out_nbs);
            // for (auto out_nb : out_nbs) {
            //     tmp_vtx.out_nbs.push_back(out_nb);
            // }

            // vector<eid_t> out_es;
            // GetConnectedEdgeList(vtx.id, 0, OUT, 0x8000000000000001, 1, out_es);
            // for (auto out_e : out_es) {
            //     tmp_vtx.out_nbs.push_back(out_e.in_v);
            // }

            // vector<vid_t> both_nbs;
            // GetConnectedVertexList(vtx.id, 0, BOTH, 0x8000000000000001, 1, both_nbs);
            // for (auto both_nb : both_nbs) {
            //     tmp_vtx.in_nbs.push_back(both_nb);
            // }

            vector<eid_t> both_es;
            GetConnectedEdgeList(vtx.id, 0, BOTH, 0x8000000000000001, 1, both_es);
            for (auto out_e : both_es) {
                if (out_e.in_v == vtx.id)
                    tmp_vtx.in_nbs.push_back(out_e.out_v);
                else
                    tmp_vtx.out_nbs.push_back(out_e.in_v);
            }

            printf(("   original: " + vtx.DebugString() + "\n").c_str());
            printf(("constructed: " + tmp_vtx.DebugString() + "\n").c_str());
        }

        vector<vid_t> all_vtx_id;
        GetAllVertex(0x8000000000000001, 1, all_vtx_id);
        printf("all vtx id: [");
        for (auto vtx_id : all_vtx_id) {
            printf("%d ", vtx_id.value());
        }
        printf("]\n");

        fflush(stdout);

        for (auto edge : hdfs_data_loader_->shuffled_edge_) {
            TMPEdge tmp_edge;
            tmp_edge.id = edge.id;

            tmp_edge.label = GetEL(edge.id, 0x8000000000000001, 1);

            // vector<pair<label_t, value_t>> properties;
            // GetEP(edge.id, 0x8000000000000001, 1, properties);
            // for (auto p : properties) {
            //     tmp_edge.ep_label_list.push_back(p.first);
            //     tmp_edge.ep_value_list.push_back(p.second);
            // }

            // for (int i = 0; i < edge.ep_label_list.size(); i++) {
            //     tmp_edge.ep_label_list.push_back(edge.ep_label_list[i]);
            //     value_t val;
            //     GetEP(epid_t(edge.id, edge.ep_label_list[i]), 0x8000000000000001, 1, val);
            //     tmp_edge.ep_value_list.push_back(val);
            // }

            vector<epid_t> epids;
            GetEPidList(edge.id, 0x8000000000000001, 1, epids);
            for (int i = 0; i < edge.ep_label_list.size(); i++) {
                tmp_edge.ep_label_list.push_back(epids[i].pid);
                value_t val;
                GetEP(epids[i], 0x8000000000000001, 1, val);
                tmp_edge.ep_value_list.push_back(val);
            }

            printf(("   original: " + edge.DebugString() + "\n").c_str());
            printf(("constructed: " + tmp_edge.DebugString() + "\n").c_str());
        }

        vector<eid_t> all_edge_id;
        GetAllEdge(0x8000000000000001, 1, all_edge_id);
        printf("all edge id: [");
        for (auto edge_id : all_edge_id) {
            printf("%d->%d ", edge_id.out_v, edge_id.in_v);
        }
        printf("]\n");

        fflush(stdout);
    }

    node_.LocalSequentialEnd();
}

vid_t DataStorage::AssignVID() {
    int vid_local = vid_to_assign_divided_++;
    return vid_t(vid_local * worker_size_ + worker_rank_);
}

vid_t DataStorage::ProcessAddVertex(const label_t& label, const uint64_t& trx_id, const uint64_t& begin_time) {
    // guaranteed that the vid is identical in the whole system
    // so that it's impossible to insert two vertex with the same vid
    vid_t vid = AssignVID();

    VertexAccessor v_accessor;
    vertex_map_.insert(v_accessor, vid.value());

    v_accessor->second.label = label;
    v_accessor->second.vp_row_list = new PropertyRowList<VertexPropertyRow>;
    v_accessor->second.ve_row_list = new TopologyRowList;

    v_accessor->second.ve_row_list->Init();
    v_accessor->second.vp_row_list->Init();

    TransactionAccessor t_accessor;
    transaction_map_.insert(t_accessor, trx_id);
    v_accessor->second.mvcc_list = new MVCCList<VertexMVCC>;

    // this won't fail as it's the first version in the list
    v_accessor->second.mvcc_list->AppendVersion(trx_id, begin_time)[0] = true;

    TransactionItem::ProcessItem q_item;
    q_item.type = TransactionItem::PROCESS_ADD_V;
    q_item.mvcc_list = v_accessor->second.mvcc_list;

    t_accessor->second.process_list.emplace_back(q_item);

    return vid;
}

bool DataStorage::ProcessDropVertex(const vid_t& vid, const uint64_t& trx_id, const uint64_t& begin_time) {
    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid.value());

    bool* mvcc_value_ptr = v_accessor->second.mvcc_list->AppendVersion(trx_id, begin_time);

    if (mvcc_value_ptr == nullptr)
        return false;

    mvcc_value_ptr[0] = false;

    TransactionAccessor t_accessor;
    transaction_map_.insert(t_accessor, trx_id);

    TransactionItem::ProcessItem q_item;
    q_item.type = TransactionItem::PROCESS_DROP_V;
    q_item.mvcc_list = v_accessor->second.mvcc_list;

    t_accessor->second.process_list.emplace_back(q_item);

    // TODO(entityless): Return edges connected to it, for DropE actors

    return true;
}

bool DataStorage::ProcessModifyVP(const vpid_t& pid, const value_t& value,
                                  const uint64_t& trx_id, const uint64_t& begin_time) {
    VertexConstAccessor v_accessor;
    vertex_map_.find(v_accessor, vid_t(pid.vid).value());
    auto ret = v_accessor->second.vp_row_list->ProcessModifyProperty(pid, value, trx_id, begin_time);

    if (ret.second == nullptr)
        return false;

    TransactionAccessor t_accessor;
    transaction_map_.insert(t_accessor, trx_id);

    TransactionItem::ProcessItem q_item;
    if (ret.first)
        q_item.type = TransactionItem::PROCESS_MODIFY_VP;
    else
        q_item.type = TransactionItem::PROCESS_ADD_VP;

    q_item.mvcc_list = ret.second;

    t_accessor->second.process_list.emplace_back(q_item);

    return true;
}

bool DataStorage::ProcessModifyEP(const epid_t& pid, const value_t& value,
                                  const uint64_t& trx_id, const uint64_t& begin_time) {
    EdgeConstAccessor e_accessor;
    edge_map_.find(e_accessor, eid_t(pid.in_vid, pid.out_vid).value());

    auto edge_item = e_accessor->second->GetVisibleVersion(trx_id, begin_time)->GetValue();

    if (!edge_item.Exist())
        return false;

    auto ret = edge_item.ep_row_list->ProcessModifyProperty(pid, value, trx_id, begin_time);

    if (ret.second == nullptr)
        return false;

    TransactionAccessor t_accessor;
    transaction_map_.insert(t_accessor, trx_id);

    TransactionItem::ProcessItem q_item;
    if (ret.first)
        q_item.type = TransactionItem::PROCESS_MODIFY_EP;
    else
        q_item.type = TransactionItem::PROCESS_ADD_EP;

    q_item.mvcc_list = ret.second;

    t_accessor->second.process_list.emplace_back(q_item);

    return true;
}

void DataStorage::Commit(const uint64_t& trx_id, const uint64_t& commit_time) {
    TransactionAccessor t_accessor;
    transaction_map_.find(t_accessor, trx_id);

    // TODO(entitlyess): Finish unfinished process functions
    for (auto process_item : t_accessor->second.process_list) {
        if (process_item.type == TransactionItem::PROCESS_MODIFY_VP ||
            process_item.type == TransactionItem::PROCESS_MODIFY_EP ||
            process_item.type == TransactionItem::PROCESS_ADD_VP ||
            process_item.type == TransactionItem::PROCESS_ADD_EP) {
            MVCCList<PropertyMVCC>* mvcc_list = process_item.mvcc_list;
            mvcc_list->CommitVersion(trx_id, commit_time);
        } else if (process_item.type == TransactionItem::PROCESS_ADD_V ||
                   process_item.type == TransactionItem::PROCESS_DROP_V) {
            MVCCList<VertexMVCC>* mvcc_list = process_item.mvcc_list;
            mvcc_list->CommitVersion(trx_id, commit_time);
        }
    }
    transaction_map_.erase(t_accessor);
}

void DataStorage::Abort(const uint64_t& trx_id) {
    TransactionAccessor t_accessor;
    transaction_map_.find(t_accessor, trx_id);

    // TODO(entitlyess): Finish unfinished process functions
    for (auto process_item : t_accessor->second.process_list) {
        if (process_item.type == TransactionItem::PROCESS_MODIFY_VP ||
            process_item.type == TransactionItem::PROCESS_ADD_VP) {
            MVCCList<PropertyMVCC>* mvcc_list = process_item.mvcc_list;
            auto header_to_free = mvcc_list->AbortVersion(trx_id);
            vp_store_->FreeValue(header_to_free);
        } else if (process_item.type == TransactionItem::PROCESS_MODIFY_EP ||
                   process_item.type == TransactionItem::PROCESS_ADD_EP) {
            MVCCList<PropertyMVCC>* mvcc_list = process_item.mvcc_list;
            auto header_to_free = mvcc_list->AbortVersion(trx_id);
            ep_store_->FreeValue(header_to_free);
        } else if (process_item.type == TransactionItem::PROCESS_DROP_V ||
            process_item.type == TransactionItem::PROCESS_ADD_V) {
            MVCCList<VertexMVCC>* mvcc_list = process_item.mvcc_list;
            mvcc_list->AbortVersion(trx_id);
        }
    }

    transaction_map_.erase(t_accessor);
}

void DataStorage::PropertyMVCCTest() {
    if (config_->HDFS_VP_SUBFOLDER != string("/chhuang/oltp/modern-data/vtx_property/"))
        return;

    if (worker_rank_ == 0) {
        uint64_t trx_ids[5] = {0x8000000000000001, 0x8000000000000002,
                               0x8000000000000003, 0x8000000000000004, 0x8000000000000005};
        uint64_t bts[5] = {1, 2, 4, 5, 7};
        uint64_t cts[5] = {3, 0, 6, 0, 0};

        vid_t victim_vid(6);
        vpid_t victim_vpid(victim_vid, 1);

        value_t n0, n1, n2, t0r0, t1r0, t1r1, t1r2, t2r0, t3r0, t3r1, t4r0;
        bool ok0, ok10, ok11, ok2;
        Tool::str2str("N0", n0);
        Tool::str2str("N1", n1);
        Tool::str2str("N2", n2);

        ok0 = ProcessModifyVP(victim_vpid, n0, trx_ids[0], bts[0]);  // modify to "N0"
        printf("Q0, %s\n", ok0 ? "true" : "false");
        GetVP(victim_vpid, trx_ids[1], bts[1], t1r0);  // Read (should be "peter")
        printf("Q1\n");
        GetVP(victim_vpid, trx_ids[0], bts[0], t0r0);  // Read (should be "N0")
        printf("Q2\n");
        ok10 = ProcessModifyVP(victim_vpid, n1, trx_ids[1], bts[1]);  // modify to "N1" (false)
        printf("Q3, %s\n", ok10 ? "true" : "false");
        GetVP(victim_vpid, trx_ids[1], bts[1], t1r1);  // Read (should be "peter"; if optimistic, or "N0")
        printf("Q4\n");
        Commit(trx_ids[0], cts[0]);
        printf("Q5\n");
        ok11 = ProcessModifyVP(victim_vpid, n1, trx_ids[1], bts[1]);  // modify to "N1"
        printf("Q6 %s\n", ok11 ? "true" : "false");
        GetVP(victim_vpid, trx_ids[1], bts[1], t1r2);  // Read (should be "N1")
        printf("Q7\n");
        GetVP(victim_vpid, trx_ids[2], bts[2], t2r0);  // Read (should be "N0")
        printf("Q8\n");
        Abort(trx_ids[1]);
        printf("Q9\n");
        ok2 = ProcessModifyVP(victim_vpid, n2, trx_ids[2], bts[2]);  // modify to "N2"
        printf("Q10, %s\n", ok0 ? "true" : "false");
        GetVP(victim_vpid, trx_ids[3], bts[3], t3r0);  // Read (should be "N0")
        printf("Q11\n");
        Commit(trx_ids[2], cts[2]);
        printf("Q12\n");
        GetVP(victim_vpid, trx_ids[3], bts[3], t3r1);  // Read (should be "N2")
        printf("Q13\n");
        GetVP(victim_vpid, trx_ids[4], bts[4], t4r0);

        printf("%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n", Tool::DebugString(t1r0).c_str(),
                                                   Tool::DebugString(t0r0).c_str(),
                                                   Tool::DebugString(t1r1).c_str(),
                                                   Tool::DebugString(t1r2).c_str(),
                                                   Tool::DebugString(t2r0).c_str(),
                                                   Tool::DebugString(t3r0).c_str(),
                                                   Tool::DebugString(t3r1).c_str(),
                                                   Tool::DebugString(t4r0).c_str());
        // all passed
    }


    node_.Rank0PrintfWithWorkerBarrier("Finished DataStorage::ModifyTest()\n");
}
