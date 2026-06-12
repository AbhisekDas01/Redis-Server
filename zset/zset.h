#pragma once

#include "AVLtrees/avl.h"
#include "hashtable/hashtable.h"

struct ZSet {
    AVLNode *root = NULL; //index by (score , name)
    HMap hmap; //index by name
};

struct ZNode {
    //data structure nodes
    AVLNode tree;
    HNode hmap;

    //data
    double score = 0;
    size_t len = 0;
    char name[0]; // Zero-length array placeholder for dynamic data

};

//helper function
bool zsetInsert(ZSet *zset , const char *name, size_t len, double score);
ZNode *zsetLookup(ZSet *zset, const char *name, size_t len);
void   zsetDelete(ZSet *zset, ZNode *node);
ZNode *zsetSeekge(ZSet *zset, double score, const char *name, size_t len);
ZNode *znodeOffset(ZNode *node, int64_t offset);
void zsetClear(ZSet *zset);