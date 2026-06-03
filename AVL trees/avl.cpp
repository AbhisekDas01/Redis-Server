#include <cassert>

#include "avl.h"

//function to find the max 
static uint32_t max(uint32_t lhs , uint32_t rhs) {
    return lhs < rhs ? rhs : lhs;
}

//function to extract the node height
static uint32_t avlHeight(AVLNode *node) {
    return node ? node->height : 0;
}

//function to update the nodes height when need
static void  avlUpdate(AVLNode *node) {
    node->height = 1 + max(avlHeight(node->left) , avlHeight(node->right));
}