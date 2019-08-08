/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Chenghuan Huang (chhuang@cse.cuhk.edu.hk)
*/


#pragma once

#include <cstdio>

#include "base/communication.hpp"
#include "base/node.hpp"
#include "core/id_mapper.hpp"
#include "layout/layout_type.hpp"
#include "layout/mpi_snapshot_manager.hpp"
#include "layout/snapshot_function_implementation.hpp"
#include "utils/config.hpp"
#include "utils/hdfs_core.hpp"
#include "utils/timer.hpp"
#include "utils/tool.hpp"

namespace std {

class HDFSDataLoader {
 private:
    Config* config_;
    Node node_;

    HDFSDataLoader() {}
    HDFSDataLoader(const HDFSDataLoader&);

    // free them after calling Shuffle()
    vector<TMPVertexInfo*> vertices_;
    vector<VProperty*> vplist_;
    vector<EProperty*> eplist_;
    hash_map<uint32_t, TMPVertex*> vtx_part_map_;

    void LoadVertices(const char* inpath);
    void LoadVPList(const char* inpath);
    void LoadEPList(const char* inpath);
    TMPVertexInfo* ToVertex(char* line);
    void ToVP(char* line);
    void ToEP(char* line);

    bool ReadVertexSnapshot();
    void WriteVertexSnapshot();
    bool ReadEdgeSnapshot();
    void WriteEdgeSnapshot();

    void GetVertices();
    void GetVPList();
    void GetEPList();
    void ShuffleVertex();
    void ShuffleEdge();

 public:
    static HDFSDataLoader* GetInstance() {
        static HDFSDataLoader* hdfs_data_loader_instance_ptr = nullptr;

        if (hdfs_data_loader_instance_ptr == nullptr) {
            hdfs_data_loader_instance_ptr = new HDFSDataLoader();
            hdfs_data_loader_instance_ptr->Init();
        }

        return hdfs_data_loader_instance_ptr;
    }

    void Init();
    void GetStringIndexes();
    void LoadVertexData();
    void LoadEdgeData();
    void FreeVertexMemory();
    void FreeEdgeMemory();

    // "schema" related
    string_index* indexes_;

    vector<TMPVertex> shuffled_vtx_;
    vector<TMPOutEdge> shuffled_out_edge_;
    vector<TMPInEdge> shuffled_in_edge_;

    // ep just follows the src_v
    // src_v -> e -> dst_v
    SimpleIdMapper* id_mapper_ = nullptr;

    MPISnapshotManager* snapshot_manager_ = nullptr;
};

}  // namespace std
