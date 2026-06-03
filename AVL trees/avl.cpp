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


//function to roate left (RR imbalance)
/*
    BEFORE
       root -> 20                                               
              /  \
            10    40  <- newRoot 
                 /  \
      inner -> 30    50  
                       \
                       60  
    AFTER
                 40  <- newRoot
                /  \
       root-> 20    50 
             /  \     \
            10   30   60  
                  ^
                  |
                inner
*/
static AVLNode *rotateLeft(AVLNode *root) {

    AVLNode *parent = root->parent;
    AVLNode *newRoot = root->right;
    AVLNode *inner = newRoot->left;

    root->right = inner; //connect the root to the internal node
    if(inner) {
        inner->parent = root;
    }

    newRoot->parent = parent; //change the parent

    newRoot->left = root; //connect the newRoots left to the root //LL rotation
    root->parent = newRoot;

    //update the heights
    avlUpdate(root);
    avlUpdate(newRoot);
    return newRoot;
}

