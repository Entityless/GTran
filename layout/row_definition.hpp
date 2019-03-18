/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Chenghuan Huang (chhuang@cse.cuhk.edu.hk)
*/


#pragma once

#include <atomic>

#include "layout/mvcc_definition.hpp"
#include "layout/mvcc_list.hpp"
#include "layout/mvcc_value_store.hpp"
#include "layout/layout_type.hpp"
#include "layout/property_row_list.hpp"

struct VPHeader {
    vpid_t pid;
    MVCCList<PropertyMVCC>* mvcc_list;
};

#define VP_ROW_ITEM_COUNT std::InferElementCount<VPHeader>(256, sizeof(void*))


struct EPHeader {
    epid_t pid;
    MVCCList<PropertyMVCC>* mvcc_list;
};

#define EP_ROW_ITEM_COUNT std::InferElementCount<EPHeader>(256, sizeof(void*))


struct EdgeHeader {
    bool is_out;  // if this vtx is a, true: a -> b, false: a <- b
    label_t label;
    vid_t conn_vtx_id;
    MVCCList<TopologyMVCC>* mvcc_list;
};

#define VE_ROW_ITEM_COUNT std::InferElementCount<EdgeHeader>(256, sizeof(void*))


// always fetch rows from memory pools


struct VertexPropertyRow {
 private:
    VertexPropertyRow* next_;
    VPHeader elements_[VP_ROW_ITEM_COUNT];

    template<class PropertyRow> friend class PropertyRowList;

 public:
    static inline int RowItemCount() {return VP_ROW_ITEM_COUNT;}

    // call this after MemPool::Get()
    void Init() {next_ = nullptr;}
}  __attribute__((aligned(64)));


struct EdgePropertyRow {
 private:
    EdgePropertyRow* next_;
    EPHeader elements_[EP_ROW_ITEM_COUNT];

    template<class PropertyRow> friend class PropertyRowList;

 public:
    static inline int RowItemCount() {return EP_ROW_ITEM_COUNT;}

    // call this after MemPool::Get()
    void Init() {next_ = nullptr;}
}  __attribute__((aligned(64)));


struct VertexEdgeRow {
 private:
    VertexEdgeRow* next_;  // need to set to nullptr after MemPool::Get()
    EdgeHeader elements_[VE_ROW_ITEM_COUNT];

    friend class TopologyRowList;

 public:
    // call this after MemPool::Get()
    void Init() {next_ = nullptr;}
}  __attribute__((aligned(64)));
