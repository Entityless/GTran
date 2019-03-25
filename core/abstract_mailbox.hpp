/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Hongzhi Chen (hzchen@cse.cuhk.edu.hk)
*/

#pragma once

#include <string>
#include <vector>
#include "core/message.hpp"
#include "base/node.hpp"

class AbstractMailbox {
 public:
    virtual ~AbstractMailbox() {}

    virtual void Init(vector<Node> & nodes) = 0;
    virtual int Send(int tid, const Message & msg) = 0;
    virtual bool TryRecv(int tid, Message & msg) = 0;
    virtual void Recv(int tid, Message & msg) = 0;
    virtual void Sweep(int tid) = 0;
    virtual void Send_Notify(int dst_nid, ibinstream& in) = 0;
    virtual void Recv_Notify(obinstream& out) = 0;
};
