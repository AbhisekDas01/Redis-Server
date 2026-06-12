#include <cstdlib>

#include "common.h"
#include "zset.h"
#include <cstring>
#include <assert.h>


// This function used to create a data entry 
static ZNode *znodeNew(const char *name , size_t len , double score) {
    ZNode *node = (ZNode *)malloc(sizeof(ZNode ) + len); //allocate the memory for the struct as well as the dynamic array in it
    avlInit(&node->tree); //init the avl node

    node->hmap.next = NULL;
    node->hmap.hcode = strHash(( uint8_t *)name , len);

    node->score = score;
    node->len = len;
    memcpy(&node->name , name , len);

    return node;
}

//function to delete a node
static void znodeDel(ZNode *node) {
    free(node);
}

static size_t min(size_t lhs, size_t rhs) {
    return lhs < rhs ? lhs : rhs;
} 

static bool zLess(AVLNode *lhs , double score , const char *name , size_t len) {
    //function to return the less node
    ZNode *zl = container_of(lhs , ZNode , tree);

    if(zl->score != score) {
        return zl->score < score;
    }

    int rv = memcmp(zl->name , name , min(zl->len , len));

    if(rv != 0) {
        return rv < 0;
    }

    return zl->len < len;
}

//function to compare nodes
static bool zLess(AVLNode *lhs , AVLNode *rhs) {
    ZNode *zl = container_of(lhs , ZNode , tree);
    ZNode *zr = container_of(rhs , ZNode , tree);

    //now check the scores 
    if(zl->score != zr->score) {
        return zl->score < zr->score;
    }

    //if the scores are same the compare the name
    int rv = memcmp(zl->name , zr->name , min(zl->len , zr->len));
    return (rv != 0)? (rv < 0) : (zl->len < zr->len);
}

//AVL find the place and insert the new node 
static void treeInsert(ZSet *zset , ZNode *node) {
    AVLNode *parent = NULL; //to hold the value of current node
    AVLNode **from = &zset->root; //pointer to the pointer of the next node

    while(*from) {
        parent = *from;
                    //node to insert compare with the other nodes
        from = zLess(&node->tree , parent)? &parent->left : &parent->right;
    }
    *from = &node->tree; // attach the new node
    node->tree.parent = parent;
    zset->root = avlFix(&node->tree);

}

//dummy structure to handle the simple lookups 
struct HKey {
    HNode node;
    const char *name = NULL;
    size_t len = 0;
};

static bool zcmp(HNode *node , HNode *key) {
    ZNode *znode = container_of(node , ZNode , hmap);
    HKey *hkey = container_of(key , HKey , node);

    if(znode->len != hkey->len) return false;

    return 0 == memcmp(znode->name , hkey->name , znode->len);
}

ZNode *zsetLookup(ZSet *zset , const char *name , size_t len) {
    if(!zset->root) {
        return NULL; // Tree is empty, key definitely doesn't exist
    }
    HKey key;
    key.node.hcode = strHash((uint8_t *)name , len);
    key.name = name;
    key.len = len;
    HNode *found = hmLookup(&zset->hmap , &key.node , &zcmp);
    return found ? container_of(found , ZNode , hmap) : NULL;

}

//function to update the entry if already exists
static void zsetUpdate(ZSet *zset , ZNode *node , double score) {
    /**
     * STEPS:
     * 1.find the node inthe hash table and update the value (Not needed since we are using a intrusive data structure)
     * 2.find the node in the avl tree and delete it 
     * 3.Insert new record in the avl tree
     */

    if(node->score == score) {
        return;
    }

    //delete the node from avl tree
    zset->root = avlDel(&node->tree);

    //create new node and insert
    avlInit(&node->tree);
    node->score = score; //insert new score in the node
    treeInsert(zset , node); //insert the new node in the tree
}

//zset insert function
bool zsetInsert(ZSet *zset , const char *name, size_t len, double score) {

    if(ZNode *node = zsetLookup(zset , name , len)) { //if node already exists then update the value
        zsetUpdate(zset , node , score);
        return false;
    }

    //create new entry
    ZNode *node = znodeNew(name , len , score);
    hmInsert(&zset->hmap , &node->hmap);
    treeInsert(zset , node);
    return true;
}

//function to delete the entry 
void zsetDelete(ZSet *zset, ZNode *node) {

    //delete the hmap entry (create a dummy node to get the node)
    HKey key;
    key.name = node->name;
    key.len = node->len;
    key.node.hcode = node->hmap.hcode;
    HNode *found = hmDelete(&zset->hmap , &key.node , &zcmp );
    assert(found);

    //delete the node from tree
    zset->root = avlDel(&node->tree);

    //deallocate the znode
    znodeDel(node);
}


int64_t zsetRank(ZSet *zset , const char *name, size_t len) {

    HKey key;
    key.name = name;
    key.len = len;
    key.node.hcode = strHash((uint8_t*)key.name , key.len);
    HNode *found = hmLookup(&zset->hmap , &key.node , &zcmp);

    if(!found) {
        return -1;
    }
    ZNode *node = container_of(found , ZNode , hmap);

    return avlRank(&node->tree) - 1;
}

//ZQUERY key score name offset limit
// At its core, ZQUERY key score name offset limit answers a complex user question like: "Give me 10 players (limit), skipping the first 500 (offset), starting exactly from a score of 1000, or alphabetically after 'Alice' if there's a score tie."

/**
 * STEPS:
 * Step 1 (Binary Seek): Dive down the AVL tree using tuple comparison to find the first node $\ge$ the requested (score, name).
 * Step 2 (Find Rank): Climb up from that node to the root, summing up skipped left-subtree cnt fields to determine its absolute starting rank.
 * Step 3 (Offset Jump): Add the user's offset to that starting rank, then use a downward binary search on cnt to teleport directly to the target page node.
 * Step 4 (Limit Stream): Walk sequentially using in-order successor pointers from that target node to harvest and return exactly limit entries.
 */


ZNode *zsetSeekge(ZSet *zset, double score, const char *name, size_t len) {
    //find a pair which is >= (score , name)
    AVLNode *found = NULL;

    for(AVLNode *node = zset->root ; node ;) {

        if(zLess(node , score , name , len)) {
            node = node->right; //node < key
        } else {
            found = node; //candiate node
            node = node->left; //check for the left to find smallest candidate
        }
    }

    return found ? container_of(found , ZNode , tree) : NULL;
}


//Get the offset 
ZNode *znodeOffset(ZNode *node, int64_t offset) {
    AVLNode *tnode = node? avl_offset(&node->tree , offset) : NULL;

    return tnode ? container_of(tnode , ZNode , tree) : NULL;
}

static void treeDispose(AVLNode *node) {
    if(!node) {
        return;
    }
    treeDispose(node->left);
    treeDispose(node->right);
    znodeDel(container_of(node , ZNode , tree));
}

void zsetClear(ZSet *zset) {

    hmClear(&zset->hmap);

    treeDispose(zset->root);
    zset->root = NULL;
}