/*
 * master.hpp
 *
 *  Created on: Jun 21, 2018
 *      Author: Hongzhi Chen
 */

#ifndef MASTER_HPP_
#define MASTER_HPP_

#include "base/node.hpp"
#include "utils/global.hpp"
#include "utils/config.hpp"

class Master{
public:
	Master(Node & node, Config * config): node_(node), config_(config){}

	void Start(){
		cout << "RANK: " << node_.get_world_rank() << " | " << node_.get_local_rank() << " DONE "<< endl;
	}

private:
	Node & node_;
	Config * config_;
};

#endif /* MASTER_HPP_ */
