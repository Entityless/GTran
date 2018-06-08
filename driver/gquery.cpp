/*
 * gquery.cpp
 *
 *  Created on: May 9, 2018
 *      Author: Hongzhi Chen
 */

#include "base/node.hpp"
#include "base/node_util.hpp"
#include "utils/global.hpp"
#include "utils/config.hpp"
#include "utils/hdfs_core.hpp"
#include "core/id_mapper.hpp"
#include "core/buffer.hpp"
#include "core/rdma_mailbox.hpp"
#include "core/actors_adapter.hpp"
#include "storage/layout.hpp"
#include "storage/data_store.hpp"

#include "glog/logging.h"


//prog node-config-fname_path host-fname_path
int main(int argc, char* argv[])
{
	google::InitGoogleLogging(argv[0]);
	init_worker(&argc, &argv);

	string node_config_fname = argv[1];
	string host_fname = argv[2];

	CHECK(!node_config_fname.empty());
	CHECK(!host_fname.empty());
	VLOG(1) << node_config_fname << " " << host_fname;

	//get nodes from config file
	std::vector<Node> nodes = ParseFile(node_config_fname);
	CHECK(CheckValidNodeIds(nodes));
	CHECK(CheckUniquePort(nodes));
	CHECK(CheckConsecutiveIds(nodes));
	Node my_node = GetNodeById(nodes, get_node_id());
	LOG(INFO) << my_node.DebugString();

	Config * config = new Config();
	config->Init();

	LOG(INFO) << "DONE -> Config->Init()";

	NaiveIdMapper * id_mapper = new NaiveIdMapper(config, my_node);
	id_mapper->Init();

	LOG(INFO) << "DONE -> NaiveIdMapper->Init()";

	//set the in-memory layout for RDMA buf
	Buffer * buf = new Buffer(config);
	buf->Init();

	LOG(INFO) << "DONE -> Buffer->Init()";

	//init the rdma mailbox
	RdmaMailbox * mailbox = new RdmaMailbox(config, id_mapper, buf);
	mailbox->Init(host_fname);

	LOG(INFO) << "DONE -> RdmaMailbox->Init()";

	DataStore * datastore = new DataStore(config, id_mapper, buf);
	datastore->Init();

	LOG(INFO) << "DONE -> DataStore->Init()";

	datastore->LoadDataFromHDFS();
	//=======data shuffle==========
	datastore->Shuffle();
	worker_barrier();

	LOG(INFO) << "DONE -> datastore->Shuffle()";
	//=======data shuffle==========

	datastore->DataConverter();

	LOG(INFO) << "DONE -> datastore->DataConverter()";

	//actor driver starts
	ActorAdapter * actor_adapter = new ActorAdapter(config, my_node, mailbox);
	actor_adapter->Start();

	worker_finalize();
	return 0;
}




