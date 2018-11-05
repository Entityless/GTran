/*
 * actor_cache.hpp
 *
 *  Created on: July 23, 2018
 *      Author: Aaron LI 
 *  Modified on: October & November, 2018
 *      Author: Chenghuan Huang 
 */
#ifndef ACTOR_CACHE_HPP_
#define ACTOR_CACHE_HPP_

#include <string>
#include <vector>
#include <type_traits>
#include <pthread.h>

#include "core/message.hpp"


#define USE_BLOCK_KEY_CACHE  1
#define BLOCK_KEY_SIZE       8
#define ACTOR_NUM_CACHE      1000000
// #define NATIVE_ARRANGE_LRU

class ActorCache {

public:

	bool get_label_from_cache(uint64_t id, label_t & label) {
		value_t val;
		if (!lookup(id, val)) {
			return false;
		}

		label = Tool::value_t2int(val);
		return true;
	}

	bool get_property_from_cache(uint64_t id, value_t & val) {
		if (!lookup(id, val)) {
			return false;
		}
		return true;
	}

	void insert_properties(uint64_t id, value_t & val) {
		insert(id, val);
	}

	void insert_label(uint64_t id, label_t & label) {
		value_t val;
		Tool::str2int(to_string(label), val);

		insert(id, val);
	}

	//commented
	// void print_cache() {
	// 	int counter = 0;
	// 	for (auto & item : items) {
	// 		if (!item.isEmpty) counter++;
	// 	}

	// 	cout << "[Cache] Use Ratio : " << to_string(counter) << "/" << to_string(NUM_CACHE) << endl;
	// }

private:

	static_assert(USE_BLOCK_KEY_CACHE == 0 || USE_BLOCK_KEY_CACHE == 1, "wrong macro USE_BLOCK_KEY_CACHE value");

#if USE_BLOCK_KEY_CACHE == 0

	struct CacheItem {
		pthread_spinlock_t lock;
		uint64_t id; // epid_t, vpid_t, eid_t, vid_t
		value_t value; // properties or labels
		bool isEmpty;

		CacheItem() {
			isEmpty = true;
			pthread_spin_init(&lock, 0);
		}
	};

	static const int NUM_CACHE = 1000000;
	static const int NUM_CACHE = ACTOR_NUM_CACHE;
	CacheItem items[NUM_CACHE];

	bool lookup(uint64_t id, value_t & val) {
		bool isFound = false;

		int key = mymath::hash_u64(id) % NUM_CACHE;

		pthread_spin_lock(&(items[key].lock));
		if (items[key].id == id) {
			val = items[key].value;
			isFound = true;
		}
		pthread_spin_unlock(&(items[key].lock));

		return isFound;
	}

	void insert(uint64_t id, value_t & val) {
		int key = mymath::hash_u64(id) % NUM_CACHE;
		pthread_spin_lock(&(items[key].lock));
		items[key].id = id;
		items[key].value = val;
		items[key].isEmpty = false;
		pthread_spin_unlock(&(items[key].lock));
	}
#else

    //keys that fill in a cache line
    //data in a specific cache block will be able to be fetched
    struct CacheBlock
    {
        //so this takes actually 128B
        //wasting 52B, to make sure that only one spin lock inside of one cache line
        // pthread_spinlock_t lock __attribute__((aligned(64)));

        //cacheline 0
        pthread_spinlock_t lock;
        int pos;

        //cacheline 1
        uint64_t id_block[BLOCK_KEY_SIZE] __attribute__((aligned(64)));

        // CacheBlock(){memset(0, id_block, 64);}
        CacheBlock()
        {
            for(int i = 0; i < BLOCK_KEY_SIZE; i++)
                {id_block[i] = 0xFFFFFFFFFFFFFFFF;}
            pos = 0;
            pthread_spin_init(&lock, 0);
        }
    };

    static_assert(sizeof(CacheBlock) == (BLOCK_KEY_SIZE * 8 + 64), "CacheBlock not aligned to 64B");

    static const int NUM_CACHE = ACTOR_NUM_CACHE / BLOCK_KEY_SIZE;

    CacheBlock blocks[NUM_CACHE];
    value_t values[NUM_CACHE][BLOCK_KEY_SIZE];

    bool lookup(uint64_t id, value_t & val) {
        int key = mymath::hash_u64(id) % NUM_CACHE;

        // int ret = pthread_spin_trylock(&(blocks[key].lock));
        // if(ret == EBUSY)
        //     return false;

        pthread_spin_lock(&(blocks[key].lock));

        //then, traversal inside the block
        for(int i = 0; i < BLOCK_KEY_SIZE; i++)
        {
            if(blocks[key].id_block[i] == id)
            {
                //found
                val = values[key][i];

#ifdef NATIVE_ARRANGE_LRU
#warning "NATIVE_ARRANGE_LRU defined"
                //do not need to run when id in the newest place
                if((i + 1) % BLOCK_KEY_SIZE != blocks[key].pos)
                {
                    int mod_pos = i, mod_pos_next;
                    while(true)
                    {
                        mod_pos_next = (mod_pos + 1) % BLOCK_KEY_SIZE;//可以减少一次取余数的运算

                        if(mod_pos_next == blocks[key].pos)
                            break;

                        blocks[key].id_block[mod_pos] = blocks[key].id_block[mod_pos_next];

                        mod_pos = mod_pos_next;
                    }

                    blocks[key].id_block[mod_pos] = id;
                }
#endif

                pthread_spin_unlock(&(blocks[key].lock));
                return true;
            }
        }

        pthread_spin_unlock(&(blocks[key].lock));

        return false;
    }

    //if it returns >0, then it means that 
    //now ring buffer has been realized
    //however, I still fail to utilize the feature of LRU
    // int insert(uint64_t id, value_t & val) {
    void insert(uint64_t id, value_t & val) {
        int key = mymath::hash_u64(id) % NUM_CACHE;
        pthread_spin_lock(&(blocks[key].lock));

        //if insert or replace needed to be recorded, then the return value can be used
        // int ret;
        // if(blocks[key].id_block[blocks[key].pos] == 0xFFFFFFFFFFFFFFFF)
        //     ret = 0;
        // else
        //     ret = 1;

        //insert into the key map
        blocks[key].id_block[blocks[key].pos] = id;
        //insert into the value map
        values[key][blocks[key].pos] = val;
        //modify the tail
        blocks[key].pos = (blocks[key].pos + 1) % BLOCK_KEY_SIZE;

        pthread_spin_unlock(&(blocks[key].lock));
        // return ret;
    }

#endif
};

#endif /* ACTOR_CACHE_HPP_ */
