/*
 * has_actor.hpp
 *
 *  Created on: July 16, 2018
 *      Author: Aaron LI
 */
#ifndef HAS_ACTOR_HPP_
#define HAS_ACTOR_HPP_

#include <string>
#include <vector>
#include <algorithm>

#include "actor/abstract_actor.hpp"
#include "actor/actor_cache.hpp"
#include "core/message.hpp"
#include "core/abstract_mailbox.hpp"
#include "core/index_store.hpp"
#include "base/type.hpp"
#include "base/predicate.hpp"
#include "storage/layout.hpp"
#include "storage/data_store.hpp"
#include "utils/tool.hpp"

class HasActor : public AbstractActor {
public:
	HasActor(int id, DataStore * data_store, int machine_id, int num_thread, AbstractMailbox * mailbox, CoreAffinity* core_affinity, bool global_enable_caching) : AbstractActor(id, data_store, core_affinity), machine_id_(machine_id), num_thread_(num_thread), mailbox_(mailbox), global_enable_caching_(global_enable_caching), type_(ACTOR_T::HAS) {}

	// Has:
	// inType
	// [	key:  int
	//		is_indexed: bool
	// 		pred: Predicate_T
	// 		pred_param: value_t]
	// Has(params) :
	// 	-> key = pid; pred = ANY; pred_params = value_t(one) : has(key)
	// 	-> key = pid; pred = EQ; pred_params = value_t(one) : has(key, value)
	// 	-> key = pid; pred = <others>; pred_params = value_t(one/two) : has(key, predicate)
	// HasValue(params) : values -> [key = -1; pred = EQ; pred_params = string(value)]
	// HasNot(params) : key -> [key = pid; pred = NONE; pred_params = -1]
	// HasKey(params) : keys -> [key = pid; pred = ANY; pred_params = -1]
	//
	// TODO : Indexing
	void process(int tid, vector<Actor_Object> & actor_objs, Message & msg) {
		// Get Actor_Object
		Meta & m = msg.meta;
		Actor_Object actor_obj = actor_objs[m.step];

		// store all predicate
		vector<pair<int, PredicateValue>> pred_chain;

		// Get Params
		assert(actor_obj.params.size() > 0 && (actor_obj.params.size() - 1) % 3 == 0); // make sure input format
		Element_T inType = (Element_T) Tool::value_t2int(actor_obj.params.at(0));
		int numParamsGroup = (actor_obj.params.size() - 1) / 3; // number of groups of params

		// Create predicate chain for this query
		for (int i = 0; i < numParamsGroup; i++) {
			int pos = i * 3 + 1;
			// Get predicate params
			int pid = Tool::value_t2int(actor_obj.params.at(pos));
			Predicate_T pred_type = (Predicate_T) Tool::value_t2int(actor_obj.params.at(pos + 1));
			vector<value_t> pred_params;
			Tool::value_t2vec(actor_obj.params.at(pos + 2), pred_params);
			pred_chain.emplace_back(pid, PredicateValue(pred_type, pred_params));
		}

		switch(inType) {
			case Element_T::VERTEX:
				EvaluateVertex(tid, msg.data, pred_chain);
				break;
			case Element_T::EDGE:
				EvaluateEdge(tid, msg.data, pred_chain);
				break;
			default:
				cout << "Wrong inType" << endl;
		}

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
	int machine_id_;

	// Actor type
	ACTOR_T type_;

	// Pointer of mailbox
	AbstractMailbox * mailbox_;

	// Cache
	ActorCache cache;
	bool global_enable_caching_;

	void EvaluateVertex(int tid, vector<pair<history_t, vector<value_t>>> & data, vector<pair<int, PredicateValue>> & pred_chain) {

		auto checkFunction = [&](value_t& value){
			vid_t v_id(Tool::value_t2int(value));
			Vertex* vtx = data_store_->GetVertex(v_id);

			for (auto & pred_pair : pred_chain) {
				int pid = pred_pair.first;
				PredicateValue pred = pred_pair.second;

				if (pid == -1) {
					int counter = vtx->vp_list.size();
					for (auto & pkey : vtx->vp_list) {
						vpid_t vp_id(v_id, pkey);

						value_t val;
						get_properties_for_vertex(tid, vp_id, val);

						if(!Evaluate(pred, &val)) {
							counter--;
						}
					}

					// Cannot match all properties, erase
					if (counter == 0) {
						return true;
					}
				} else {
					// Check whether key exists for this vtx
					if (find(vtx->vp_list.begin(), vtx->vp_list.end(), pid) == vtx->vp_list.end()) {
						// does not exist
						if (pred.pred_type == Predicate_T::NONE)
							return false;
						return true;
					}
					if (pred.pred_type == Predicate_T::ANY)
						return false;

					// Get Properties
					vpid_t vp_id(v_id, pid);
					value_t val;
					get_properties_for_vertex(tid, vp_id, val);

					// Erase when doesnt match
					if(!Evaluate(pred, &val)) {
						return true;
					}
				}
			}

			return false;
		};

		for (auto & data_pair : data) {
			data_pair.second.erase( remove_if(data_pair.second.begin(), data_pair.second.end(), checkFunction), data_pair.second.end() );
		}
	}

	void EvaluateEdge(int tid, vector<pair<history_t, vector<value_t>>> & data, vector<pair<int, PredicateValue>> & pred_chain) {

		auto checkFunction = [&](value_t& value){
			eid_t e_id;
			uint2eid_t(Tool::value_t2uint64_t(value), e_id);
			Edge* edge = data_store_->GetEdge(e_id);

			for (auto & pred_pair : pred_chain) {
				int pid = pred_pair.first;
				PredicateValue pred = pred_pair.second;

				if (pid == -1) {
					int counter = edge->ep_list.size();
					for (auto & pkey : edge->ep_list) {
						epid_t ep_id(e_id, pkey);
						value_t val;
						get_properties_for_edge(tid, ep_id, val);

						if(!Evaluate(pred, &val)) {
							counter--;
						}
					}

					// Cannot match all properties, erase
					if (counter == 0) {
						return true;
					}
				} else {
					// Check whether key exists for this vtx
					if (find(edge->ep_list.begin(), edge->ep_list.end(), pid) == edge->ep_list.end()) {
						// does not exist
						if (pred.pred_type == Predicate_T::NONE)
							return false;
						return true;
					}

					if (pred.pred_type == Predicate_T::ANY)
						return false;

					// Get Properties
					epid_t ep_id(e_id, pid);
					value_t val;
					get_properties_for_edge(tid, ep_id, val);

					// Erase when doesnt match
					if(!Evaluate(pred, &val)) {
						return true;
					}
				}
			}

			return false;
		};

		for (auto & data_pair : data) {
			data_pair.second.erase( remove_if(data_pair.second.begin(), data_pair.second.end(), checkFunction), data_pair.second.end() );
		}
	}

	void get_properties_for_vertex(int tid, vpid_t vp_id, value_t & val) {
		if (data_store_->VPKeyIsLocal(vp_id) || !global_enable_caching_) {
			// No Need to check Cache for local or cache is disabled
			data_store_->GetPropertyForVertex(tid, vp_id, val);
		} else {
			if (!cache.get_property_from_cache(vp_id.value(), val)) {
				data_store_->GetPropertyForVertex(tid, vp_id, val);
				cache.insert_properties(vp_id.value(), val);
			}
		}
	}

	void get_properties_for_edge(int tid, epid_t ep_id, value_t & val) {
		if (data_store_->EPKeyIsLocal(ep_id) || !global_enable_caching_) {
			data_store_->GetPropertyForEdge(tid, ep_id, val);
		} else {
			if (!cache.get_property_from_cache(ep_id.value(), val)) {
				data_store_->GetPropertyForEdge(tid, ep_id, val);
				cache.insert_properties(ep_id.value(), val);
			}
		}
	}
};

#endif /* HAS_ACTOR_HPP_ */
