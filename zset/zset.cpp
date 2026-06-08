#include <cstdlib>

#include "common.h"
#include "zset.h"
#include <cstring>


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