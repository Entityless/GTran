/* Copyright 2019 Husky Data Lab, CUHK

Authors: Created by Chenghuan Huang (chhuang@cse.cuhk.edu.hk)
*/


#include "hdfs_data_loader.hpp"

using namespace std;

void HDFSDataLoader::Initial()
{
    config_ = Config::GetInstance();
    node_ = Node::StaticInstance();

    indexes_ = new string_index;

    // node_.LocalSequentialDebugPrint("HDFSDataLoader::Initial() finished");
}

void HDFSDataLoader::GetStringIndexes()
{
    hdfsFS fs = get_hdfs_fs();

    string el_path = config_->HDFS_INDEX_PATH + "./edge_label";
    hdfsFile el_file = get_r_handle(el_path.c_str(), fs);
    LineReader el_reader(fs, el_file);
    while(true)
    {
        el_reader.read_line();
        if (!el_reader.eof())
        {
            char * line = el_reader.get_line();
            char * pch;
            pch = strtok(line, "\t");
            string key(pch);
            pch = strtok(NULL, "\t");
            label_t id = atoi(pch);

            // both string and ID are unique
            assert(indexes_->str2el.find(key) == indexes_->str2el.end());
            assert(indexes_->el2str.find(id) == indexes_->el2str.end());

            indexes_->str2el[key] = id;
            indexes_->el2str[id] = key;
        }
        else
            break;
    }
    hdfsCloseFile(fs, el_file);

    string epk_path = config_->HDFS_INDEX_PATH + "./edge_property_index";
    hdfsFile epk_file = get_r_handle(epk_path.c_str(), fs);
    LineReader epk_reader(fs, epk_file);
    while(true)
    {
        epk_reader.read_line();
        if (!epk_reader.eof())
        {
            char * line = epk_reader.get_line();
            char * pch;
            pch = strtok(line, "\t");
            string key(pch);
            pch = strtok(NULL, "\t");
            label_t id = atoi(pch);
            pch = strtok(NULL, "\t");
            edge_pty_key_to_type_[to_string(id)] = atoi(pch);

            // both string and ID are unique
            assert(indexes_->str2epk.find(key) == indexes_->str2epk.end());
            assert(indexes_->epk2str.find(id) == indexes_->epk2str.end());

            indexes_->str2epk[key] = id;
            indexes_->epk2str[id] = key;
        }
        else
            break;
    }
    hdfsCloseFile(fs, epk_file);

    string vl_path = config_->HDFS_INDEX_PATH + "./vtx_label";
    hdfsFile vl_file = get_r_handle(vl_path.c_str(), fs);
    LineReader vl_reader(fs, vl_file);
    while(true)
    {
        vl_reader.read_line();
        if (!vl_reader.eof())
        {
            char * line = vl_reader.get_line();
            char * pch;
            pch = strtok(line, "\t");
            string key(pch);
            pch = strtok(NULL, "\t");
            label_t id = atoi(pch);

            // both string and ID are unique
            assert(indexes_->str2vl.find(key) == indexes_->str2vl.end());
            assert(indexes_->vl2str.find(id) == indexes_->vl2str.end());

            indexes_->str2vl[key] = id;
            indexes_->vl2str[id] = key;
        }
        else
            break;
    }
    hdfsCloseFile(fs, vl_file);

    string vpk_path = config_->HDFS_INDEX_PATH + "./vtx_property_index";
    hdfsFile vpk_file = get_r_handle(vpk_path.c_str(), fs);
    LineReader vpk_reader(fs, vpk_file);
    while(true)
    {
        vpk_reader.read_line();
        if (!vpk_reader.eof())
        {
            char * line = vpk_reader.get_line();
            char * pch;
            pch = strtok(line, "\t");
            string key(pch);
            pch = strtok(NULL, "\t");
            label_t id = atoi(pch);
            pch = strtok(NULL, "\t");
            vtx_pty_key_to_type_[to_string(id)] = atoi(pch);

            // both string and ID are unique
            assert(indexes_->str2vpk.find(key) == indexes_->str2vpk.end());
            assert(indexes_->vpk2str.find(id) == indexes_->vpk2str.end());

            indexes_->str2vpk[key] = id;
            indexes_->vpk2str[id] = key;
        }
        else
            break;
    }
    hdfsCloseFile(fs, vpk_file);
    hdfsDisconnect(fs);
}

void HDFSDataLoader::GetVertices()
{
    const char * indir = config_->HDFS_VTX_SUBFOLDER.c_str();

    // topo
    if (node_.get_local_rank() == MASTER_RANK)
    {
        vector<vector<string>> arrangement = dispatch_locality(indir, node_.get_local_size());
        master_scatter(node_, false, arrangement);
        vector<string>& assigned_splits = arrangement[0];
        //reading assigned splits (map)
        for (vector<string>::iterator it = assigned_splits.begin(); it != assigned_splits.end(); it++)
            LoadVertices(it->c_str());
    }
    else
    {
        vector<string> assigned_splits;
        slave_scatter(node_, false, assigned_splits);
        //reading assigned splits (map)
        for (vector<string>::iterator it = assigned_splits.begin(); it != assigned_splits.end(); it++)
            LoadVertices(it->c_str());
    }
}

void HDFSDataLoader::LoadVertices(const char* inpath)
{
    hdfsFS fs = get_hdfs_fs();
    hdfsFile in = get_r_handle(inpath, fs);
    LineReader reader(fs, in);
    while (true)
    {
        reader.read_line();
        if (!reader.eof()){
            Vertex * v = ToVertex(reader.get_line());
            vertices_.push_back(v);
        }else{
            break;
        }
    }
    hdfsCloseFile(fs, in);
    hdfsDisconnect(fs);
}

//Format
//vid [\t] #in_nbs [\t] nb1 [space] nb2 [space] ... #out_nbs [\t] nb1 [space] nb2 [space] ...
Vertex* HDFSDataLoader::ToVertex(char* line)
{
    Vertex * v = new Vertex;

    char * pch;
    pch = strtok(line, "\t");

    vid_t vid(atoi(pch));
    v->id = vid;

    pch = strtok(NULL, "\t");
    int num_in_nbs = atoi(pch);
    for(int i = 0 ; i < num_in_nbs; i++){
        pch = strtok(NULL, " ");
        v->in_nbs.push_back(atoi(pch));
    }
    pch = strtok(NULL, "\t");
    int num_out_nbs = atoi(pch);
    for(int i = 0 ; i < num_out_nbs; i++){
        pch = strtok(NULL, " ");
        v->out_nbs.push_back(atoi(pch));
    }
    return v;
}

void HDFSDataLoader::GetVPList()
{
    const char * indir = config_->HDFS_VP_SUBFOLDER.c_str();

    if (node_.get_local_rank() == MASTER_RANK)
    {
        if(dir_check(indir) == -1)
            exit(-1);
    }

    if (node_.get_local_rank() == MASTER_RANK)
    {
        vector<vector<string>> arrangement = dispatch_locality(indir, node_.get_local_size());
        master_scatter(node_, false, arrangement);
        vector<string>& assigned_splits = arrangement[0];
        //reading assigned splits (map)
        for (vector<string>::iterator it = assigned_splits.begin(); it != assigned_splits.end(); it++)
            LoadVPList(it->c_str());
    }
    else
    {
        vector<string> assigned_splits;
        slave_scatter(node_, false, assigned_splits);
        //reading assigned splits (map)
        for (vector<string>::iterator it = assigned_splits.begin(); it != assigned_splits.end(); it++)
            LoadVPList(it->c_str());
    }

    // node_.LocalSequentialDebugPrint("HDFSDataLoader::GetVPList() finished");
}

void HDFSDataLoader::LoadVPList(const char* inpath)
{
    hdfsFS fs = get_hdfs_fs();
    hdfsFile in = get_r_handle(inpath, fs);
    LineReader reader(fs, in);
    while (true)
    {
        reader.read_line();
        if (!reader.eof())
            ToVP(reader.get_line());
        else
            break;
    }
    hdfsCloseFile(fs, in);
    hdfsDisconnect(fs);
}

//Format
//vid [\t] label[\t] [kid:value,kid:value,...]
void HDFSDataLoader::ToVP(char* line)
{
    VProperty * vp = new VProperty;

    char * pch;
    pch = strtok(line, "\t");
    vid_t vid(atoi(pch));
    vp->id = vid;

    pch = strtok(NULL, "\t");
    label_t label = (label_t)atoi(pch);

    // insert label to VProperty
    V_KVpair v_pair;
    v_pair.key = vpid_t(vid, 0);
    Tool::str2int(to_string(label),v_pair.value);
    //push to property_list of v
    vp->plist.push_back(v_pair);

    pch = strtok(NULL, "");
    string s(pch);

    vector<string> kvpairs;
    Tool::splitWithEscape(s, "[],:", kvpairs);
    assert(kvpairs.size() % 2 == 0);
    for(int i = 0 ; i < kvpairs.size(); i += 2){
        kv_pair p;
        Tool::get_kvpair(kvpairs[i], kvpairs[i+1], vtx_pty_key_to_type_[kvpairs[i]], p);
        V_KVpair v_pair;
        v_pair.key = vpid_t(vid, p.key);
        v_pair.value = p.value;

        //push to property_list of v
        vp->plist.push_back(v_pair);
    }

    //sort p_list in vertex
    vplist_.push_back(vp);
}

void HDFSDataLoader::GetEPList()
{
    const char * indir = config_->HDFS_EP_SUBFOLDER.c_str();

    if (node_.get_local_rank() == MASTER_RANK)
    {
        vector<vector<string>> arrangement = dispatch_locality(indir, node_.get_local_size());
        master_scatter(node_, false, arrangement);
        vector<string>& assigned_splits = arrangement[0];
        //reading assigned splits (map)
        for (vector<string>::iterator it = assigned_splits.begin(); it != assigned_splits.end(); it++)
            LoadEPList(it->c_str());
    }
    else
    {
        vector<string> assigned_splits;
        slave_scatter(node_, false, assigned_splits);
        //reading assigned splits (map)
        for (vector<string>::iterator it = assigned_splits.begin(); it != assigned_splits.end(); it++)
            LoadEPList(it->c_str());
    }
    // node_.LocalSequentialDebugPrint("HDFSDataLoader::GetEPList() finished");
}


void HDFSDataLoader::LoadEPList(const char* inpath)
{
    hdfsFS fs = get_hdfs_fs();
    hdfsFile in = get_r_handle(inpath, fs);
    LineReader reader(fs, in);
    while (true)
    {
        reader.read_line();
        if (!reader.eof())
            ToEP(reader.get_line());
        else
            break;
    }
    hdfsCloseFile(fs, in);
    hdfsDisconnect(fs);
}

//Format
//out-v[\t] in-v[\t] label[\t] [kid:value,kid:value,...]
void HDFSDataLoader::ToEP(char* line)
{
    Edge * e = new Edge;
    EProperty * ep = new EProperty;

    uint64_t atoi_time = timer::get_usec();
    char * pch;
    pch = strtok(line, "\t");
    int out_v = atoi(pch);
    pch = strtok(NULL, "\t");
    int in_v = atoi(pch);

    eid_t eid(in_v, out_v);
    e->id = eid;
    ep->id = eid;

    pch = strtok(NULL, "\t");
    label_t label = (label_t)atoi(pch);
    // insert label to EProperty
    E_KVpair e_pair;
    e_pair.key = epid_t(in_v, out_v, 0);
    Tool::str2int(to_string(label),e_pair.value);
    //push to property_list of v
    ep->plist.push_back(e_pair);

    pch = strtok(NULL, "");
    string s(pch);
    vector<label_t> pkeys;

    vector<string> kvpairs;
    Tool::splitWithEscape(s, "[],:", kvpairs);
    assert(kvpairs.size() % 2 == 0);

    for(int i = 0 ; i < kvpairs.size(); i += 2){
        kv_pair p;
        Tool::get_kvpair(kvpairs[i], kvpairs[i+1], edge_pty_key_to_type_[kvpairs[i]], p);

        E_KVpair e_pair;
        e_pair.key = epid_t(in_v, out_v, p.key);
        e_pair.value = p.value;

        ep->plist.push_back(e_pair);
        pkeys.push_back((label_t)p.key);

    }

    sort(pkeys.begin(), pkeys.end());
    e->ep_list.insert(e->ep_list.end(), pkeys.begin(), pkeys.end());
    edges_.push_back(e);
    eplist_.push_back(ep);
}

void HDFSDataLoader::LoadData()
{
    GetStringIndexes();
    GetVertices();
    GetVPList();
    GetEPList();

    // node_.LocalSequentialDebugPrint("HDFSDataLoader::LoadData() finished");
}

void HDFSDataLoader::Shuffle()
{
    //vertices_
    vector<vector<Vertex*>> vtx_parts;
    vtx_parts.resize(node_.get_local_size());
    for (int i = 0; i < vertices_.size(); i++)
    {
        Vertex* v = vertices_[i];
        vtx_parts[VidMapping(v->id)].push_back(v);
    }
    all_to_all(node_, false, vtx_parts);
    vertices_.clear();
    if(node_.get_local_rank() == 0){
        cout << "Shuffle vertex done" << endl;
    }

    for (int i = 0; i < node_.get_local_size(); i++)
    {
        vertices_.insert(vertices_.end(), vtx_parts[i].begin(), vtx_parts[i].end());
    }
    vtx_parts.clear();
    vector<vector<Vertex*>>().swap(vtx_parts);

    //edges_
    vector<vector<Edge*>> edges_parts;
    edges_parts.resize(node_.get_local_size());
    for (int i = 0; i < edges_.size(); i++)
    {
        Edge* e = edges_[i];
        edges_parts[EidMapping(e->id)].push_back(e);
    }
    all_to_all(node_, false, edges_parts);
    if(node_.get_local_rank() == 0){
        cout << "Shuffle edge done" << endl;
    }

    edges_.clear();
    for (int i = 0; i < node_.get_local_size(); i++)
    {
        edges_.insert(edges_.end(), edges_parts[i].begin(), edges_parts[i].end());
    }
    edges_parts.clear();
    vector<vector<Edge*>>().swap(edges_parts);

    //VProperty
    vector<vector<VProperty*>> vp_parts;
    vp_parts.resize(node_.get_local_size());
    for (int i = 0; i < vplist_.size(); i++)
    {
        VProperty* vp = vplist_[i];
        map<int, vector<V_KVpair>> node_map;
        for(auto& kv_pair : vp->plist)
            node_map[VPidMapping(kv_pair.key)].push_back(kv_pair);
        for(auto& item : node_map){
            VProperty* vp_ = new VProperty();
            vp_->id = vp->id;
            vp_->plist = move(item.second);
            vp_parts[item.first].push_back(vp_);
        }
        delete vp;
    }
    all_to_all(node_, false, vp_parts);
    
    if(node_.get_local_rank() == 0){
        cout << "Shuffle vp done" << endl;
    }

    vplist_.clear();

    for (int i = 0; i < node_.get_local_size(); i++)
    {
        vplist_.insert(vplist_.end(), vp_parts[i].begin(), vp_parts[i].end());
    }
    vp_parts.clear();
    vector<vector<VProperty*>>().swap(vp_parts);

    // EProperty
    // TODO(entityless): optimiza 
    vector<vector<EProperty*>> ep_parts;
    ep_parts.resize(node_.get_local_size());
    for (int i = 0; i < eplist_.size(); i++)
    {
        EProperty* ep = eplist_[i];
        map<int, vector<E_KVpair>> node_map;
        for(auto& kv_pair : ep->plist)
        {
            node_map[EPidMapping(kv_pair.key)].push_back(kv_pair);
            if(kv_pair.key.pid == 0)
                node_map[EPidMappingIn(kv_pair.key)].push_back(kv_pair);
        }
        for(auto& item : node_map){
            EProperty* ep_ = new EProperty();
            ep_->id = ep->id;
            ep_->plist = move(item.second);
            ep_parts[item.first].push_back(ep_);
        }
        delete ep;
    }

    all_to_all(node_, false, ep_parts);

    if(node_.get_local_rank() == 0){
        cout << "Shuffle ep done" << endl;
    }

    eplist_.clear();
    for (int i = 0; i < node_.get_local_size(); i++)
    {
        eplist_.insert(eplist_.end(), ep_parts[i].begin(), ep_parts[i].end());
    }
    ep_parts.clear();
    vector<vector<EProperty*>>().swap(ep_parts);
    // node_.LocalSequentialDebugPrint("HDFSDataLoader::Shuffle() finished");

    shuffled_vtx_.resize(vertices_.size());
    shuffled_edge_.resize(edges_.size());

    // construct TMPVertex
    int auto_iter_counter;

    auto_iter_counter = 0;
    for (auto vtx_topo : vertices_)
    {
        TMPVertex& vtx_ref = shuffled_vtx_[auto_iter_counter];
        vtx_part_map_[vtx_topo->id.vid] = &vtx_ref;

        vtx_ref.id = vtx_topo->id;
        vtx_ref.in_nbs = vtx_topo->in_nbs;
        vtx_ref.out_nbs = vtx_topo->out_nbs;

        auto_iter_counter++;
    }

    // construct TMPEdge
    auto_iter_counter = 0;
    for (auto edge : edges_)
    {
        TMPEdge& edge_ref = shuffled_edge_[auto_iter_counter];
        edge_part_map_[edge->id.value()] = &edge_ref;

        edge_ref.id = edge->id;

        auto_iter_counter++;
    }

    // insert vp
    for(auto vps : vplist_)
    {
        TMPVertex& vtx_ref = *vtx_part_map_[vps->id.vid];

        for(auto vp : vps->plist)
        {
            // label
            if(vp.key.pid == 0)
            {
                vtx_ref.label = Tool::value_t2int(vp.value);
            }
            else
            {
                vtx_ref.vp_label_list.push_back(vp.key.pid);
                vtx_ref.vp_value_list.push_back(vp.value);
            }
        }
    }

    // insert ep
    for(auto eps : eplist_)
    {
        // just construct a new edge to insert the label
        if(edge_part_map_.count(eps->id.value()) == 0)
        {
            edge_part_map_[eps->id.value()] = new TMPEdge;
            edge_part_map_[eps->id.value()]->id = eid_t(eps->plist[0].key.in_vid, eps->plist[0].key.out_vid);
            edge_part_map_[eps->id.value()]->label = Tool::value_t2int(eps->plist[0].value);
        }

        TMPEdge& edge_ref = *edge_part_map_[eps->id.value()];

        for(auto ep : eps->plist)
        {
            // label
            if(ep.key.pid == 0)
            {
                edge_ref.label = Tool::value_t2int(ep.value);
            }
            else
            {
                edge_ref.ep_label_list.push_back(ep.key.pid);
                edge_ref.ep_value_list.push_back(ep.value);
            }
        }
    }

    node_.Rank0PrintfWithWorkerBarrier("HDFSDataLoader::Construct all finished\n");

    node_.LocalSequentialDebugPrint(to_string(shuffled_vtx_.size()));
    node_.LocalSequentialDebugPrint(to_string(shuffled_edge_.size()));

    // debug print
    string dbg_str = "";
    for(auto v : shuffled_vtx_)
    {
        dbg_str += "\n" + v.DebugString();
    }
    // for(auto e : shuffled_edge_)  // property edge only
    // {
    //     dbg_str += "\n" + e.DebugString();
    // }
    for(auto e : edge_part_map_)  // property edge and ghost edge
    {
        assert(e.first == e.second->id.value());
        dbg_str += "\n" + e.second->DebugString();
    }
    node_.LocalSequentialDebugPrint(dbg_str);

    // free shuffled buffer's memory
    for(auto p : edges_)
        delete p;
    for(auto p : vertices_)
        delete p;
    for(auto p : vplist_)
        delete p;
    for(auto p : eplist_)
        delete p;
    vector<Edge*>().swap(edges_);
    vector<Vertex*>().swap(vertices_);
    vector<VProperty*>().swap(vplist_);
    vector<EProperty*>().swap(eplist_);
}

void HDFSDataLoader::FreeMemory()
{
    vtx_part_map_ = hash_map<uint32_t, TMPVertex*>();
    edge_part_map_ = hash_map<uint64_t, TMPEdge*>();
    vector<TMPVertex>().swap(shuffled_vtx_);
    vector<TMPEdge>().swap(shuffled_edge_);
}
