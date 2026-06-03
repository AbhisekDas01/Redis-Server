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
    if(inner) { // if inner node actually exist then update its parent
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

//function to handle right rotation (LL imbalance)
/*
   
                       50 <- root
                      /  \
         newRoot ->  40  10    
                    /  \
                   5    30  <- inner
                  /
                 60 
*/

static AVLNode *rotateRight(AVLNode *root) {

    AVLNode *parent = root->parent;
    AVLNode *newRoot = root->left;
    AVLNode *inner = newRoot->right;

    root->left = inner;
    if(inner) {
        inner->parent = root;
    }

    newRoot->parent = parent;

    newRoot->right = root;
    root->parent = newRoot;

    avlUpdate(root);
    avlUpdate(newRoot);
    return newRoot;
}

//Rotation for the left side imbalance
static AVLNode *avlFixLeft(AVLNode *root) {
    if(avlHeight(root->left->left) < avlHeight(root->left->right)) { //LR rotation
        root->left = rotateLeft(root->left); //1.Right rotaion of the one half to make the LL imbalance
    }
    return rotateRight(root); //RR rotation 
}
//rotation for the right side imbalance 
static AVLNode *avlFixRight(AVLNode *root) {

    if(avlHeight(root->right->right) < avlHeight(root->right->left)) {
        root->right = rotateRight(root->right);
    }
    return rotateLeft(root);
}