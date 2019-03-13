/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Chenghuan Huang (chhuang@cse.cuhk.edu.hk)
*/

#pragma once

#include <assert.h>
#include <memory.h>
#include <pthread.h>
#include <stdint.h>

#include <atomic>
#include <cstdio>
#include <string>

#define OFFSET_MEMORY_POOL_DEBUG

// if element_cnt is small than 65535, OffsetT can be set to uint16_t
// if element_cnt is larger than 4G, OffsetT can be set to uint64_t
template<class ItemT, class OffsetT = uint32_t>
class OffsetConcurrentMemPool {
 private:
    OffsetConcurrentMemPool() {}
    OffsetConcurrentMemPool(const OffsetConcurrentMemPool&);
    ~OffsetConcurrentMemPool() {if(next_offset_ != nullptr) delete[] next_offset_;}

    void Init(ItemT* mem, size_t element_cnt);

    // TODO(entityless): find out a way to avoid false sharing
    ItemT* attached_mem_;
    size_t element_cnt_;

    #ifdef OFFSET_MEMORY_POOL_DEBUG
    std::atomic_int get_counter_, free_counter_;
    #endif  // OFFSET_MEMORY_POOL_DEBUG

    // simple implemention with acceptable performance
    OffsetT head_, tail_;
    OffsetT* next_offset_ = nullptr;

    pthread_spinlock_t head_lock_, tail_lock_;

 public:
    static OffsetConcurrentMemPool* GetInstance(ItemT* mem = nullptr, size_t element_cnt = -1) {
        static OffsetConcurrentMemPool* p = nullptr;

        // null and var avail
        if (p == nullptr && element_cnt > 0) {
            p = new OffsetConcurrentMemPool();
            p->Init(mem, element_cnt);
        }

        return p;
    }

    ItemT* Get();
    void Free(ItemT* element);
    #ifdef OFFSET_MEMORY_POOL_DEBUG
    std::string UsageString();
    #endif  // OFFSET_MEMORY_POOL_DEBUG
};

#include "concurrent_mem_pool.tpp"
