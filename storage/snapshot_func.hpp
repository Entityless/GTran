/*-----------------------------------------------------
       @copyright (c) 2018 CUHK Husky Data Lab
              Last modified : 2018-11
  Author(s) : Chenghuan Huang(entityless@gmail.com)
:)
-----------------------------------------------------*/
#pragma once

//this file is designed to be isolated from MPISnapshot

#include "base/serialization.hpp"
//namespace std is used there

#include <ext/hash_map>

using __gnu_cxx::hash_map;

//the most simple

template<typename T>
static inline bool WriteSerImpl(string fn, T& data)
{
    ofstream doge(fn, ios::binary);

    if(!doge.is_open())
    {
        return false;
    }

    ibinstream m;
    m << data;

    uint64_t buf_sz = m.size();
    doge.write((char*)&buf_sz, sizeof(uint64_t));
    // doge << m.size();

    doge.write(m.get_buf(), m.size());

    doge.close();

    return true;
}

template<typename T>
static inline bool ReadSerImpl(string fn, T& data)
{
    ifstream doge(fn, ios::binary);

    if(!doge.is_open())
    {
        return false;
    }

    uint64_t sz;
    // doge >> sz;
    doge.read((char*)&sz, sizeof(uint64_t));

    char* tmp_buf = new char[sz];
    doge.read(tmp_buf, sz);
    doge.close();

    obinstream m;
    m.assign(tmp_buf, sz, 0);

    m >> data;

    return true;
}


template<typename T1,typename T2>
static inline bool WriteHashMapSerImpl(string fn, hash_map<T1, T2*>& data)
{
    ofstream doge(fn, ios::binary);

    if(!doge.is_open())
    {
        return false;
    }

    //write data to the instream
    ibinstream m;

    for(auto kv : data)
    {
        //notice that kv.second is a pointer
        //and the content of the pointer need to be written

        m << kv.first;//the key
        m << *kv.second;//the value is a pointer
    }

    uint64_t data_sz = data.size(), buf_sz = m.size();
    printf("WriteHashMapSerImpl data_sz = %d, buf_sz = %d\n", data_sz, buf_sz);

    // doge << data_sz;
    // doge << buf_sz;
    doge.write((char*)&data_sz, sizeof(uint64_t));
    doge.write((char*)&buf_sz, sizeof(uint64_t));
    doge.write(m.get_buf(), m.size());

    doge.close();

    return true;
}

template<typename T1,typename T2>
static inline bool ReadHashMapSerImpl(string fn, hash_map<T1, T2*>& data)
{
    ifstream doge(fn, ios::binary);

    if(!doge.is_open())
    {
        return false;
    }

    uint64_t buf_sz, data_sz;
    // doge >> data_sz;
    // doge >> buf_sz;
    doge.read((char*)&data_sz, sizeof(uint64_t));
    doge.read((char*)&buf_sz, sizeof(uint64_t));

    printf("ReadHashMapSerImpl data_sz = %d, buf_sz = %d\n", data_sz, buf_sz);

    char* tmp_buf = new char[buf_sz];
    doge.read(tmp_buf, buf_sz);
    doge.close();

    obinstream m;
    m.assign(tmp_buf, buf_sz, 0);

    for(uint64_t i = 0; i < data_sz; i++)
    {
        T1 key;
        T2* value = new T2;

        m >> key >> *value;

        data[key] = value;
    }

    return true;
}

//
static inline bool WriteKVStoreImpl(string fn, tuple<uint64_t, uint64_t, char*>& data)
{
    ofstream doge(fn, ios::binary);

    if(!doge.is_open())
    {
        return false;
    }

    //write data to the instream
    ibinstream m;

    // auto [last_entry, mem_sz, mem] = data;?????? not support?

    uint64_t last_entry = get<0>(data), mem_sz = get<1>(data);
    char* mem = get<2>(data);

    printf("WriteKVStoreImpl last_entry = %lu, mem_sz = %lu\n", last_entry, mem_sz);

    doge.write((char*)&last_entry, sizeof(uint64_t));
    doge.write((char*)&mem_sz, sizeof(uint64_t));
    doge.write(mem, mem_sz);

    doge.close();

    return true;
}

//
static inline bool ReadKVStoreImpl(string fn, tuple<uint64_t, uint64_t, char*>& data)
{
    ifstream doge(fn, ios::binary);

    if(!doge.is_open())
    {
        return false;
    }

    //write data to the instream
    obinstream m;

    char* mem = get<2>(data);

    uint64_t last_entry, mem_sz;

    doge.read((char*)&last_entry, sizeof(uint64_t));
    doge.read((char*)&mem_sz, sizeof(uint64_t));

    printf("ReadKVStoreImpl last_entry = %d, mem_sz = %lu\n", last_entry, mem_sz);

    doge.read(mem, mem_sz);

    doge.close();

    data = make_tuple(last_entry, mem_sz, mem);

    return true;
}
