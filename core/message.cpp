/*
 * message.cpp
 *
 *  Created on: Jun 8, 2018
 *      Author: Hongzhi Chen
 */

#include "core/message.hpp"

ibinstream& operator<<(ibinstream& m, const Meta& meta)
{
	m << meta.qid;
	m << meta.step;
	m << meta.sender;
	m << meta.recver;
	m << meta.msg_type;
	m << meta.chains;
	return m;
}

obinstream& operator>>(obinstream& m, Meta& meta)
{
	m >> meta.qid;
	m >> meta.step;
	m >> meta.sender;
	m >> meta.recver;
	m >> meta.msg_type;
	m >> meta.chains;
	return m;
}

std::string Meta::DebugString() const {
	std::stringstream ss;
	ss << "Meta: {";
	ss << "  qid: " << qid;
	ss << ", step: " << step;
	ss << ", sender node: " << sender;
	ss << ", recver node: " << recver;
	ss << ", msg type: " << MsgType[static_cast<int>(msg_type)];
	ss << ", query chains: [";
	for(auto c : chains){
		ss  << ActorType[static_cast<int>(c)] << ", ";
	}
	ss << "]}";
	return ss.str();
}

ibinstream& operator<<(ibinstream& m, const Message& msg)
{
	m << msg.meta;
	m << msg.data;
	return m;
}

obinstream& operator>>(obinstream& m, Message& msg)
{
	m >> msg.meta;
	m >> msg.data;
	return m;
}

std::string Message::DebugString() const {
	std::stringstream ss;
	ss << meta.DebugString();
	if (data.size()) {
	  ss << " Body:";
	  for (const auto& d : data)
		ss << " data_size=" << d.size();
	}
	return ss.str();
}

Message CreateMessage(MSG_T _type, int _qid, int _step, int _sender, int _recver,
		vector<ACTOR_T> _chains, SArray<char> data) {
	Message m;
	m.meta.msg_type = _type;
	m.meta.qid = _qid;
	m.meta.step = _step;
	m.meta.sender = _sender;
	m.meta.recver = _recver;

	if (_chains.size() != 0)
		m.meta.chains.insert(m.meta.chains.end(), _chains.begin(), _chains.end());
	if (data.size() != 0)
		m.AddData(data);
	return m;
}
