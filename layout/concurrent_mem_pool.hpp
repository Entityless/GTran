/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Chenghuan Huang (chhuang@cse.cuhk.edu.hk)
*/

#pragma once

#include <assert.h>
#include <memory.h>
#include <pthread.h>
#include <stdint.h>

#if defined(__INTEL_COMPILER)
#include <malloc.h>
#else
#include <mm_malloc.h>
#endif  // defined(__GNUC__)

#include <atomic>
#include <cstdio>
#include <string>


#define OFFSET_MEMORY_POOL_DEBUG

// if element_count is small than 65535, OffsetT can be set to uint16_t
// if element_count is larger than 4G, OffsetT can be set to uint64_t
template<class ItemT, class OffsetT = uint32_t, int BLOCK_SIZE = 2048>
class ConcurrentMemPool {
 private:
    ConcurrentMemPool() {}
    ConcurrentMemPool(const ConcurrentMemPool&);
    ~ConcurrentMemPool();

    void Init(ItemT* mem, OffsetT element_count, int nthreads);

    bool mem_allocated_ __attribute__((aligned(16))) = false;
    ItemT* attached_mem_ __attribute__((aligned(16))) = nullptr;
    OffsetT* next_offset_ __attribute__((aligned(16))) = nullptr;

    #ifdef OFFSET_MEMORY_POOL_DEBUG
    OffsetT element_count_;
    int nthreads_;
    std::atomic_int get_counter_, free_counter_;
    #endif  // OFFSET_MEMORY_POOL_DEBUG

    // simple implemention with acceptable performance
    OffsetT head_ __attribute__((aligned(64)));
    OffsetT tail_ __attribute__((aligned(64)));

    pthread_spinlock_t lock_ __attribute__((aligned(64)));

    // the user should guarantee that a specific tid will only be used by one specific thread.
    struct ThreadStat {
        OffsetT block_head __attribute__((aligned(16)));
        OffsetT block_tail __attribute__((aligned(16)));
        OffsetT free_cell_count __attribute__((aligned(16)));
    } __attribute__((aligned(64)));

    static_assert(sizeof(ThreadStat) % 64 == 0, "concurrent_mem_pool.hpp, sizeof(ThreadStat) % 64 != 0");

    ThreadStat* thread_stat_ __attribute__((aligned(64)));

 public:
    static ConcurrentMemPool* GetInstance(ItemT* mem = nullptr, OffsetT element_count = -1, int nthreads = 1) {
        static ConcurrentMemPool* p = nullptr;

        // null and var avail
        if (p == nullptr && element_count > 0) {
            p = new ConcurrentMemPool();
            p->Init(mem, element_count, nthreads);
        }

        return p;
    }

    ItemT* Get(int tid = 0);
    void Free(ItemT* element, int tid = 0);
    #ifdef OFFSET_MEMORY_POOL_DEBUG
    std::string UsageString();
    #endif  // OFFSET_MEMORY_POOL_DEBUG
};

#include "concurrent_mem_pool.tpp"
