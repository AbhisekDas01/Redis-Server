#pragma once //Only include this file once during the compilation of a single source file, no matter how many times it gets requested.


#include <cstdint> //used for uint64_t
#include <cstddef> //for NULL && size_t

//intrusive list node. An intrusive hashtable doesn’t care about the data, but it still needs the hash value for the insertion.
// See explanation: hashtable.md#a-intrusive-data-structure
struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0; //it will hold the hash value of each node
};

//Define the fixed-size hashtable
struct HTable {
    HNode **tab = NULL; //array of the buckets used in the hash table
    size_t mask = 0; //the size of the arrya should be multiple of 2 , mask = 2^n-1
    size_t size = 0; //number of keys stored currently
};

//Create a resizable hashmap 

//The resizable HMap is based on the fixed-size HTab. It contains 2 of them for the progressive rehashing.
// See explanation: hashtable.md#b-progressive-incremental-rehashing
struct HMap {
    HTable newer;
    HTable older;
    size_t migratePos = 0; //it is used to store the last index that has migrated to the newer hashtable
};


//get set and delete interface
HNode *hmLookup(HMap *hmap , HNode *key , bool (*eq)(HNode * , HNode*));
void hmInsert(HMap *hmap , HNode *node);
HNode *hmDelete(HMap *hmap , HNode *key , bool (*eq)(HNode* , HNode*));