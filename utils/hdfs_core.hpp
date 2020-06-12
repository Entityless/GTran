// Copyright 2019 BigGraph Team @ Husky Data Lab, CUHK
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef HDFS_CORE_HPP_
#define HDFS_CORE_HPP_

#include <hdfs.h>
#include <mpi.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "utils/global.hpp"


static const int HDFS_BUF_SIZE = 65536;
static const int LINE_DEFAULT_SIZE = 4096;
static const int HDFS_BLOCK_SIZE = 8388608;  // 8M
static string HDFS_HOST_ADDRESS = "";
static int HDFS_PORT = -1;

// ====== get File System ======
void hdfs_init(string _host, int _port);

hdfsFS get_hdfs_fs();

hdfsFS get_local_fs();

// ======== HDFS Delete ========

int hdfs_delete(hdfsFS& fs, const char* outdir, int flag = 1);

// ====== get File Handle ======

hdfsFile get_r_handle(const char* path, hdfsFS fs);

hdfsFile get_w_handle(const char* path, hdfsFS fs);

hdfsFile get_rw_handle(const char* path, hdfsFS fs);

// ====== Read line ======

// logic:
// buf[] is for batch reading from HDFS file
// line[] is a line buffer, the string length is "length", the buffer size is "size"
// after each readLine(), need to check eof(), if it's true, no line is read due to EOF
class LineReader {
 public:
    // dynamic fields
    char* line;
    int length;
    int size;

    LineReader(hdfsFS& fs, hdfsFile& handle);

    ~LineReader();

    // internal use only!
    void double_linebuf();

    // internal use only!
    void line_append(const char* first, int num);

    // internal use only!
    void fill();

    bool eof();

    // user interface
    // the line starts at "line", with "length" chars
    void append_line();

    void read_line();

    char* get_line();

 private:
    // static fields
    char buf_[HDFS_BUF_SIZE];
    tSize buf_pos_;
    tSize buf_size_;
    hdfsFS fs_;
    hdfsFile handle_;
    bool file_end_;
};

// ====== Dir Creation ======
void dir_create(const char* outdir);

// ====== Dir Check ======
int out_dir_check(const char* outdir, bool print, bool force);  // returns -1 if fail, 0 if succeed

int dir_check(const char* indir, const char* outdir, bool print, bool force);  // returns -1 if fail, 0 if succeed

int dir_check(vector<string>& indirs, const char* outdir, bool print, bool force);  // returns -1 if fail, 0 if succeed

int dir_check(const char* indir, vector<string>& outdirs, bool print, bool force);  // returns -1 if fail, 0 if succeed

int dir_check(const char* outdir, bool force);  // returns -1 if fail, 0 if succeed

int dir_check(const char* indir);  // returns -1 if fail, 0 if succeed

// ====== Write line ======

class LineWriter {
 public:
    LineWriter(const char* path, hdfsFS fs, int me);

    ~LineWriter();

    // internal use only!
    void next_hdl();

    void write_line(char* line, int num);

 private:
    hdfsFS fs_;
    hdfsFile cur_hdl_;

    const char* path_;
    int me_;  // -1 if there's no concept of machines (like: hadoop fs -put)
    int nxt_part_;
    int cur_size_;
};

// ====== Put: local->HDFS ======

void put(const char* localpath, const char* hdfspath);

void putf(const char* localpath, const char* hdfspath);  // force put, overwrites target

// ====== Put: all local files under dir -> HDFS ======

void put_dir(const char* localpath, const char* hdfspath);

// ====== BufferedWriter ======
struct BufferedWriter {
 public:
    BufferedWriter(const char* path, hdfsFS fs);
    BufferedWriter(const char* path, hdfsFS fs, int me);

    ~BufferedWriter();

    // internal use only!
    void next_hdl();

    void check();

    void write(const char* content);

 private:
    hdfsFS fs_;
    const char* path_;
    int me_;  // -1 if there's no concept of machines (like: hadoop fs -put)
    int next_part_;
    vector<char> buf_;
    hdfsFile cur_hdl_;
};

// ====== Dispatcher ======

struct sizedFName {
    char* fname;
    tOffset size;

    bool operator<(const sizedFName& o) const;
};

struct sizedFString {
    string fname;
    tOffset size;

    bool operator<(const sizedFString& o) const;
};

const char* rfind(const char* str, char delim);

vector<vector<string>> dispatch_run(const char* in_dir, int num_slaves);

vector<vector<string>> dispatch_locality(const char* in_dir, int num_slaves);

void report_assignment(vector<vector<string>> assignment, int num_slaves);


#endif /* HDFS_CORE_HPP_ */
