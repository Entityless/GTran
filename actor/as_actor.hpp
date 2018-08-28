/*
 * as_actor.hpp
 *
 *  Created on: July 24, 2018
 *      Author: Aaron LI
 */
#ifndef AS_ACTOR_HPP_
#define AS_ACTOR_HPP_

#include <string>
#include <vector>

#include "actor/abstract_actor.hpp"
#include "core/message.hpp"
#include "core/abstract_mailbox.hpp"
#include "base/type.hpp"
#include "base/predicate.hpp"
#include "storage/layout.hpp"
#include "storage/data_store.hpp"
#include "utils/tool.hpp"

class AsActor : public AbstractActor {
public:
	AsActor(int id, DataStore* data_store, int num_thread, AbstractMailbox * mailbox, CoreAffinity* core_affinity) : AbstractActor(id, data_store, core_affinity), num_thread_(num_thread), mailbox_(mailbox), type_(ACTOR_T::AS) {}

	void process(int tid, vector<Actor_Object> & actor_objs, Message & msg) {
		// Get Actor_Object
		Meta & m = msg.meta;
		Actor_Object actor_obj = actor_objs[m.step];

		// Get Params
		int label_step_key = Tool::value_t2int(actor_obj.params.at(0));

		// record history_t
		RecordHistory(label_step_key, msg.data);

		// Create Message
		vector<Message> msg_vec;
		msg.CreateNextMsg(actor_objs, msg.data, num_thread_, data_store_, core_affinity_, msg_vec);

		// Send Message
		for (auto& msg : msg_vec) {
			mailbox_->Send(tid, msg);
		}
	 }

private:
	// Number of Threads
	int num_thread_;

	// Actor type
	ACTOR_T type_;

	// Pointer of mailbox
	AbstractMailbox * mailbox_;

	void RecordHistory(int label_step_key, vector<pair<history_t, vector<value_t>>> & data) {	
		vector<pair<history_t, vector<value_t>>> newData;
		map<value_t, int> value_pos;
		int cnt;

		for (auto & data_pair : data) {
			value_pos.clear();
			cnt = 0;
			for (auto & elem : data_pair.second) {
				history_t his;
				vector<value_t> newValue;

				// Check whether elem is already in newData
				map<value_t, int>::iterator itr = value_pos.find(elem);
				if (itr == value_pos.end()) {
					// move previous history to new one
					his = data_pair.first;

					// push back current label key
					his.emplace_back(label_step_key, elem);

					newValue.push_back(elem);
					newData.emplace_back(his, newValue);

					// Insert into map
					value_pos.insert(pair<value_t, int>(elem, cnt++));
				} else {
					// append elem to certain history
					int pos = value_pos.at(elem);

					if (pos >= value_pos.size()) {
						cout << "Position larger than size : " << pos << " & " << value_pos.size() << endl;
					} else {
						newData.at(pos).second.push_back(elem);
					}
				}
			}
		}

		/*
		for (auto & data_pair : newData) {
			cout << "----------------" << endl;
			for (auto & his : data_pair.first) {
				cout << Tool::DebugString(his.second) << ",";
			}
			cout << " : ";
			for (auto & val : data_pair.second) {
				cout << Tool::DebugString(val) << ",";
			}
			cout << "." << endl;
		}*/

		data.swap(newData);
	}
};

#endif /* AS_ACTOR_HPP_ */
