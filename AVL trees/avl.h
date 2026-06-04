#pragma once

#include <cstdint> //used for uint64_t
#include <cstddef> //for NULL && size_t

struct AVLNode {
    /*To move in 3 directions for rotations use 3 pointers*/
    AVLNode *parent = NULL;
    AVLNode *left = NULL;
    AVLNode *right = NULL;

    uint32_t height = 0; //height of the subtree
};

inline void avlInit(AVLNode *node) {
    node->left = node->right = node->parent = NULL;
    node->height = 1;
}

// API
AVLNode *avlFix(AVLNode *node);
AVLNode *avlDel(AVLNode *node);