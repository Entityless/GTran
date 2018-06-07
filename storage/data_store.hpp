/*
 * gquery.cpp
 *
 *  Created on: May 29, 2018
 *      Author: Hongzhi Chen
 */

#pragma once

#include <string.h>
#include <stdlib.h>
#include <ext/hash_map>
#include <ext/hash_set>

#include <hdfs.h>
#include "glog/logging.h"

#include "utils/hdfs_core.hpp"
#include "utils/config.hpp"
#include "utils/unit.hpp"
#include "utils/type.hpp"
#include "utils/tool.hpp"
#include "utils/global.hpp"
#include "core/id_mapper.hpp"
#include "core/buffer.hpp"
#include "base/communication.hpp"
#include "storage/vkvstore.hpp"
#include "storage/ekvstore.hpp"

using __gnu_cxx::hash_map;
using __gnu_cxx::hash_set;

class DataStore {
public:
	DataStore(Config * config, AbstractIdMapper * id_mapper, Buffer * buf);

	~DataStore();

	void Init();

	//index format
	//string \t index [int]
	/*
	 * 	unordered_map<string, label_t> str2el; //map to edge_label
	 * 	unordered_map<label_t, string> el2str;
	 *	unordered_map<string, label_t> str2epk; //map to edge's property key
	 *	unordered_map<label_t, string> epk2str;
	 *	unordered_map<string, label_t> str2vl; //map to vtx_label
	 *	unordered_map<label_t, string> vl2str;
	 *	unordered_map<string, label_t> str2vpk; //map to vtx's property key
	 *	unordered_map<label_t, string> vpk2str;
	 */

	void LoadDataFromHDFS();
	void Shuffle();


	void DataConverter();

	Vertex* GetVertex(vid_t v_id);

	Edge* GetEdge(eid_t e_id);


	void GetPropertyForVertex(int tid, vpid_t vp_id, value_t & val);


	void GetPropertyForEdge(int tid, epid_t ep_id, value_t & val);

private:

	Buffer * buffer_;
	AbstractIdMapper* id_mapper_;
	Config* config_;

	//load the index and data from HDFS
	string_index indexes; //index is global, no need to shuffle
	hash_map<vid_t, Vertex*> v_table;
	hash_map <eid_t, Edge*> e_table;

    VKVStore * vpstore_;
    EKVStore * epstore_;
	//=========tmp usage=========

	vector<Vertex*> vertices;
	vector<Edge*> edges;
	vector<VProperty*> vplist;
	vector<EProperty*> eplist;

	hash_map<vid_t, uint32_t> vtx_offset_map;
	hash_map<eid_t, uint32_t> edge_offset_map;

	uint32_t vtx_count;
	uint32_t edge_count;

	//==========tmp usage=========

	void get_string_indexes();
	void get_vertices();

	void load_vertices(const char* inpath);

	Vertex* to_vertex(char* line);

	void get_edges();

	void load_edges(const char* inpath);

	Edge* to_edge(char* line);

	void get_vplist();

	void load_vplist(const char* inpath);

	VProperty* to_vp(char* line);

	void get_eplist();

	void load_eplist(const char* inpath);

	EProperty* to_ep(char* line);
};
