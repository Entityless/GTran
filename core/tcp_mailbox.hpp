#pragma once

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <iostream>
#include <unordered_map>
#include <string>
#include <fstream>
#include <sstream>

#include "base/node_util.hpp"
#include "core/abstract_mailbox.hpp"
#include "core/message.hpp"
#include "utils/zmq.hpp"

#include <tbb/concurrent_unordered_map.h>

using namespace std;

class TCPMailbox : public AbstractMailbox { 
private:
    typedef tbb::concurrent_unordered_map<int, zmq::socket_t *> socket_map;
    typedef vector<zmq::socket_t *> socket_vector;

    // The communication over zeromq, a socket library.
    zmq::context_t context;
    socket_vector receivers_;
    socket_map senders_;

	Node & my_node_;
	Config * config_;

    pthread_spinlock_t *locks;

	inline int port_code (int nid, int tid) { return nid * config_->global_num_threads + tid; }

public:

    TCPMailbox(Node & my_node, Config * config) : my_node_(my_node), config_(config), context(1) {}

	~TCPMailbox();

    void Init(vector<Node> & nodes) override;
    int Send(int tid, const Message & msg) override;
	int Send(int tid, const mailbox_data_t & data) override;
    void Recv(int tid, Message & msg) override ;
    bool TryRecv(int tid, Message & msg) override;
	void Sweep(int tid) override;
};
