/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Changji Li (cjli@cse.cuhk.edu.hk)

*/

#include "core/tcp_mailbox.hpp"

TCPMailbox::~TCPMailbox() {
    for (auto &r : receivers_)
        if (r != NULL) delete r;

    for (auto &s : senders_) {
        if (s.second != NULL) {
            delete s.second;
            s.second = NULL;
        }
    }
}

void TCPMailbox::Init(vector<Node> & nodes) {
    receivers_.resize(config_->global_num_threads);
    for (int tid = 0; tid < config_->global_num_threads; tid++) {
        receivers_[tid] = new zmq::socket_t(context, ZMQ_PULL);
        char addr[64] = "";
        snprintf(addr, sizeof(addr), "tcp://*:%d", my_node_.tcp_port + 1 + tid);
        receivers_[tid]->bind(addr);
    }

    for (int nid = 0; nid < config_->global_num_workers; nid++) {
        Node & r_node = GetNodeById(nodes, nid + 1);
        string ibname = r_node.ibname;

        for (int tid = 0; tid < config_->global_num_threads; tid++) {
            int pcode = port_code(nid, tid);

            senders_[pcode] = new zmq::socket_t(context, ZMQ_PUSH);
            char addr[64] = "";
            snprintf(addr, sizeof(addr), "tcp://%s:%d", ibname.c_str(), r_node.tcp_port + 1 + tid);
            // FIXME: check return value
            senders_[pcode]->connect(addr);
        }
    }

    locks = (pthread_spinlock_t *)malloc(sizeof(pthread_spinlock_t) * (config_->global_num_threads * config_->global_num_workers));
    for (int n = 0; n < config_->global_num_workers; n++) {
        for (int t = 0; t < config_->global_num_threads; t++)
            pthread_spin_init(&locks[n * config_->global_num_threads + t], 0);
    }
}

int TCPMailbox::Send(int tid, const Message & msg) {
    int pcode = port_code(msg.meta.recver_nid, msg.meta.recver_tid);

    ibinstream m;
    m << msg;

    zmq::message_t zmq_msg(m.size());
    memcpy((void *)zmq_msg.data(), m.get_buf(), m.size());

    pthread_spin_lock(&locks[pcode]);
    if (senders_.find(pcode) == senders_.end()) {
        cout << "Cannot find dst_node port num" << endl;
        return 0;
    }

    senders_[pcode]->send(zmq_msg, ZMQ_DONTWAIT);
    pthread_spin_unlock(&locks[pcode]);
}

int TCPMailbox::Send(int tid, const mailbox_data_t & data) {
    int pcode = port_code(data.dst_nid, data.dst_tid);

    zmq::message_t zmq_msg(data.stream.size());
    memcpy((void *)zmq_msg.data(), data.stream.get_buf(), data.stream.size());

    pthread_spin_lock(&locks[pcode]);
    if (senders_.find(pcode) == senders_.end()) {
        cout << "Cannot find dst_node port num" << endl;
        return 0;
    }

    senders_[pcode]->send(zmq_msg, ZMQ_DONTWAIT);
    pthread_spin_unlock(&locks[pcode]);
}

bool TCPMailbox::TryRecv(int tid, Message & msg) {
    zmq::message_t zmq_msg;
    obinstream um;

    if (receivers_[tid]->recv(&zmq_msg) < 0) {
        cout << "Node " << my_node_.get_local_rank() << " recvs with error " << strerror(errno) << std::endl;
        return false;
    } else {
        char* buf = new char[zmq_msg.size()];
        memcpy(buf, zmq_msg.data(), zmq_msg.size());
        um.assign(buf, zmq_msg.size(), 0);
        um >> msg;
        return true;
    }
}

void TCPMailbox::Recv(int tid, Message & msg) { return; }
void TCPMailbox::Sweep(int tid) { return; }
void TCPMailbox::Send_Notify(int dst_nid, ibinstream& in) { return; }
void TCPMailbox::Recv_Notify(obinstream& out) { return ; }
