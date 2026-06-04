#pragma once

#include <cstdint> //used for uint64_t
#include <cstddef> //for NULL && size_t

struct AVLNode {
    /*To move in 3 directions for rotations use 3 pointers*/
    AVLNode *parent = NULL;
    AVLNode *left = NULL;
    AVLNode *right = NULL;

    uint32_t height = 0; //height of the subtree
    uint32_t cnt = 0;       // subtree size

};

inline void avlInit(AVLNode *node) {
    node->left = node->right = node->parent = NULL;
    node->height = 1;
    node->cnt = 1;
}

//function to extract the node height
inline uint32_t avlHeight(AVLNode *node) {
    return node ? node->height : 0;
}

inline uint32_t avlCnt(AVLNode *node) { return node ? node->cnt : 0; }


// API
AVLNode *avlFix(AVLNode *node);
AVLNode *avlDel(AVLNode *node);