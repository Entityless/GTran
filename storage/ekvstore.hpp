/*
 * ekvstore.hpp
 *
 *  Created on: Jun 1, 2018
 *      Author: Aaron Li, Hongzhi Chen
 */

#ifndef EKVSTORE_HPP_
#define EKVSTORE_HPP_

#include <stdint.h> // uint64_t
#include <string.h>
#include <vector>
#include <iostream>
#include <pthread.h>

#include "base/rdma.hpp"
#include "utils/mymath.hpp"
#include "utils/type.hpp"
#include "utils/unit.hpp"
#include "utils/config.hpp"
#include "core/buffer.hpp"
#include "storage/layout.hpp"

class EKVStore {
public:

    // EKVStore: key (main-header and indirect-header region) | value (entry region)
    //         head region is a cluster chaining hash-table (with associativity)
    //         entry region is a varying-size array
    // For Vertex-Properties
    EKVStore(Config * config, Buffer * buf);

    void init();

    // Insert a list of Vertex properties
    void insert_edge_properties(vector<EProperty*> & eplist);

    // Get properties by key locally
    void get_property_local(uint64_t pid, elem_t & elem);

    //TODO: tid --> SEND_BUF MAY NOT BE thread-safe
    // Get properties by key remotely
    void get_property_remote(int tid, int dst_nid, uint64_t pid, elem_t & elem);

    // analysis
    void print_mem_usage();

private:

    Config * config_;
    Buffer * buf_;

    static const int NUM_LOCKS = 1024;

    static const int ASSOCIATIVITY = 8;  // the associativity of slots in each bucket

    // Memory Layout:
    //     Header - Entry : 70% : 30%
    //     Header:
    //        Main_Header: 80% Header
    //        Extra_Header: 20% Header
    //     Entry:
    //        Size get from user
    //
    //    These two parameters should be put into config later
    static const int HD_RATIO = 70; // header / (header + entry)
    static const int MHD_RATIO = 80; // main-header / (main-header + indirect-header)

    // size of ekvstore and offset to rdma start point
    char* mem;
    uint64_t mem_sz;
    uint64_t offset;

    // kvstore key
    ikey_t *keys;
    // kvstore value
    char* values;

    uint64_t num_slots;       // 1 bucket = ASSOCIATIVITY slots
    uint64_t num_buckets;     // main-header region (static)
    uint64_t num_buckets_ext; // indirect-header region (dynamical)
    uint64_t num_entries;     // entry region (dynamical)

    // used
    uint64_t last_ext;
    uint64_t last_entry;

    pthread_spinlock_t entry_lock;
    pthread_spinlock_t bucket_ext_lock;
    pthread_spinlock_t bucket_locks[NUM_LOCKS]; // lock virtualization (see paper: vLokc CGO'13)

    // cluster chaining hash-table (see paper: DrTM SOSP'15)
    uint64_t insert_id(uint64_t _pid);

    // Insert all properties for one edge
    void insert_single_edge_property(EProperty* ep);

    uint64_t sync_fetch_and_alloc_values(uint64_t n);

    void get_key_local(uint64_t pid, ikey_t & key);

    //TODO: tid --> SEND_BUF MAY NOT BE thread-safe
    void get_key_remote(int tid, int dst_nid, uint64_t pid, ikey_t & key);
};


#endif /* EKVSTORE_HPP_ */
