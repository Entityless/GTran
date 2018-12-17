/*
 * abstract_actor.hpp
 *
 *  Created on: May 26, 2018
 *      Author: Hongzhi Chen
 */
//
#ifndef ABSTRACT_ACTOR_HPP_
#define ABSTRACT_ACTOR_HPP_

#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include "base/core_affinity.hpp"
#include "core/message.hpp"
#include "storage/data_store.hpp"
#include "utils/tid_mapper.hpp"


// #define ACTOR_PROCESS_PRINT
// #define ACTOR_PROCESS_SLEEP 100000000L //only with print, did sleep matters

#define ACTOR_DBG 0

#define ACTOR_DBG_PRINTF(Format...) {if(ACTOR_DBG) {printf(Format);}}

class AbstractActor {
public:
	AbstractActor(int id, DataStore* data_store, CoreAffinity* core_affinity):id_(id), data_store_(data_store), core_affinity_(core_affinity){}

	virtual ~AbstractActor() {}


	const int GetActorId(){return id_;}


	virtual void process(const vector<Actor_Object> & actors, Message & msg) = 0;

protected:
	// Data Store
    DataStore* data_store_;

	// Core affinity
	CoreAffinity* core_affinity_;

private:
	// Actor ID
	int id_;
};

#endif /* ABSTRACT_ACTOR_HPP_ */
