/*
 * message.hpp
 *
 *  Created on: May 15, 2018
 *      Author: Hongzhi Chen
 */

#pragma once

#include <vector>
#include <sstream>

#include "base/sarray.hpp"
#include "base/serialization.hpp"
#include "base/type.hpp"
#include "actor/actor_object.hpp"

struct Meta {
  // query
  uint64_t qid;
  int step;

  // route
  int recver_nid;
  int recver_tid;

  // parent route
  int parent_nid;
  int parent_tid;

  // type
  MSG_T msg_type;

  // Msg disptching path
  string msg_path;

  // branch info
  // extended in CreatBranchedMsg
  // removed in MsgServer
  vector<pair<int, int>> branch_route;  // route to collect branch
  vector<int> branch_mid;               // id_ of derived message
  vector<string> branch_path;           // msg_path of derived message

  // actors chain
  vector<Actor_Object> actors;

  std::string DebugString() const ;
};

ibinstream& operator<<(ibinstream& m, const Meta& meta);

obinstream& operator>>(obinstream& m, Meta& meta);

typedef vector<pair<int, value_t>> history_t;

class Message {
public:
	Meta meta;

	std::vector<pair<history_t, vector<value_t>>> data;

  // size of data
  size_t data_size;
  // maximum size of data
  size_t max_data_size;

  Message() : data_size(sizeof(size_t)), max_data_size(1048576){}
  Message(const Meta& m) : Message()
  {
    meta = m;
  }

  // Feed in data, remove from source
  void FeedData(pair<history_t, vector<value_t>>& pair);
  void FeedData(vector<pair<history_t, vector<value_t>>>& vec);
  // Copy data from source
  void CopyData(vector<pair<history_t, vector<value_t>>>& vec);

  // create init msg
  // currently
  // recv_tid = qid % thread_pool.size()
  // parent_node = _my_node.get_local_rank()
  static void CreatInitMsg(uint64_t qid, int parent_node, int nodes_num, int recv_tid, vector<Actor_Object>& actors, int max_data_size, vector<Message>& vec);

  // actors:  actors chain for current message
  // data:    new data processed by actor_type
  // vec:     messages to be send
  // mapper:  function that maps value_t to particular machine, default NULL
  void CreatNextMsg(vector<Actor_Object>& actors, vector<pair<history_t, vector<value_t>>>& data, vector<Message>& vec, int (*mapper)(value_t&) = NULL);

  // actors:  actors chain for current message
  // stpes:   branching steps
  // msg_id:  assigned by actor to indicate parent msg
  // vec:     messages to be send
  void CreatBranchedMsg(vector<Actor_Object>& actors, vector<int>& steps, int msg_id, vector<Message>& vec);

  // Move acotrs chain from message
  void GetActors(vector<Actor_Object>& vec);

	std::string DebugString() const;
};

ibinstream& operator<<(ibinstream& m, const Message& msg);

obinstream& operator>>(obinstream& m, Message& msg);

// Move to Branch and Barrier Actor
class MsgServer{
public:
  MsgServer();

  // Collect msg and determine msg completed or NOT
  // if msg_type != BARRIER, BRANCH
  //    return true
  // else
  //    run path_counter_ to check and merge data to msg_map_
  bool ConsumeMsg(Message& msg);

  // get msg info for collecting sub msg
  void GetMsgInfo(Message& msg, size_t &id, string &end_path);

  // Check if all sub msg are collected
  bool IsReady(uint64_t id, string end_path, string msg_path);
private:
  map<uint64_t, map<string, int>> path_counter_;
  map<uint64_t, Message> msg_map_;
};

size_t MemSize(int i);
size_t MemSize(char c);
size_t MemSize(value_t data);

template<class T1, class T2>
size_t MemSize(pair<T1, T2> p);

template<class T>
size_t MemSize(vector<T> data);
