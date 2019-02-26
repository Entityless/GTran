/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Hongzhi Chen (hzchen@cse.cuhk.edu.hk)

*/

#pragma once

#include <emmintrin.h>
#include <vector>
#include <string>
#include <mutex>

#include "core/buffer.hpp"
#include "core/message.hpp"
#include "core/abstract_mailbox.hpp"
#include "core/abstract_id_mapper.hpp"
#include "base/node.hpp"
#include "base/rdma.hpp"
#include "base/serialization.hpp"
#include "utils/config.hpp"
#include "utils/global.hpp"

#include "glog/logging.h"

#define CLINE 64

class RdmaMailbox : public AbstractMailbox {
 public:
    RdmaMailbox(Node & node, Node & master, Buffer * buffer) :
        node_(node), master_(master), buffer_(buffer) {
        config_ = Config::GetInstance();
    }

    virtual ~RdmaMailbox() {}

    void Init(vector<Node> & nodes) override;

    // When sent to the same recv buffer, the consistency relies on
    // the lock in the id_mapper
    int Send(int tid, const Message & msg) override;

    int Send(int tid, const mailbox_data_t & data) override;

    void Recv(int tid, Message & msg) override;

    bool TryRecv(int tid, Message & msg) override;

    void Sweep(int tid) override;

    void Send_Notify(int dst_nid, ibinstream& in) override;

    void Recv_Notify(obinstream& out) override;

 private:
    struct rbf_rmeta_t {
        uint64_t tail;  // write from here
        pthread_spinlock_t lock;
    } __attribute__((aligned(CLINE)));

    struct rbf_lmeta_t {
        uint64_t head;  // read from here
        pthread_spinlock_t lock;
    } __attribute__((aligned(CLINE)));

    // each thread uses a round-robin strategy to check its physical-queues
    struct scheduler_t {
        uint64_t rr_cnt;  // round-robin
    } __attribute__((aligned(CLINE)));


    bool CheckRecvBuf(int tid, int nid);
    void FetchMsgFromRecvBuf(int tid, int nid, obinstream & um);
    bool IsBufferFull(int dst_nid, int dst_tid, uint64_t tail, uint64_t msg_sz);
    bool SendData(int tid, const mailbox_data_t& data);

    Node & node_;
    Node & master_;
    Config* config_;
    Buffer * buffer_;

    vector<vector<mailbox_data_t>> pending_msgs;

    rbf_rmeta_t *rmetas = NULL;
    rbf_lmeta_t *lmetas = NULL;
    pthread_spinlock_t *recv_locks = NULL;
    scheduler_t *schedulers;
};
