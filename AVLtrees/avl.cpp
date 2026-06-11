#include <cassert>

#include "avl.h"

//function to find the max 
static uint32_t max(uint32_t lhs , uint32_t rhs) {
    return lhs < rhs ? rhs : lhs;
}

//function to update the nodes height when need
static void  avlUpdate(AVLNode *node) {
    node->height = 1 + max(avlHeight(node->left) , avlHeight(node->right));
    node->cnt = 1 + avlCnt(node->left) + avlCnt(node->right);
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


//function to initiate the operation of the rotations
AVLNode *avlFix(AVLNode *root) {

   
    while(true) {
         AVLNode **from = &root; //stores the physical memory addres of the root pointer (pointer to pointer
         AVLNode *parent = root->parent;

         if(parent) { //if the parent exists then check on which side the root exists 
            from = parent->left == root? &parent->left : &parent->right; //(store the address of the left or right pointers to the from so that we can direcly used them  )
        }

        avlUpdate(root);

        uint32_t l = avlHeight(root->left);
        uint32_t r = avlHeight(root->right);

        //fix the imbalance (max 2)
        if(l == r + 2) { //left side imbalance
            *from = avlFixLeft(root); // here the *from means (parent->left /parent->right)
        } else if(l + 2 == r) {
            *from = avlFixRight(root);
        }

        if(!parent) { //root node reached return
            return *from;
        }

        root = parent;  // continue to the parent node because its height may be changed
    }

}


/**
 * 
 * AVL Node detach function
 * 2 CASES
 * -> AT MOST one children 
 * -> 2 childrens
 */

 //function to delete the node having atmost 1 child
static AVLNode * avlDelOneChild(AVLNode *node) {

    assert(!node->right || !node->left);
    AVLNode *child = node->left ? node->left : node->right; //extract the child
    AVLNode *parent = node->parent;

    //update the child parent
    if(child) {
        child->parent = parent;
    }

    //if we are deleting the root node (parent doesnot exists) 
    if(!parent) {
        return child;
    }

    //we have to link the parents left/right child to the child node
    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child; //link to appropriate pointer

    return avlFix(parent);
}

//function to delete node with 2 chilrens
AVLNode *avlDel(AVLNode *node) {

    //if the node has 0 or one children
    if(!node->left || !node->right) {
        return avlDelOneChild(node);
    }

    //nodes having 2 childs 
    /**
     * STEPS:
     * 1. find the successor node from the right subtree (left most element of right subtree (smaller than right and larger than left subtree))
     * 2. That successor node must have 0 or 1 child (Call the detach node function on the successor) return value = rootNode;
     * 3. swap the target node with the succesor node 
     * 4. Connect all the links
     * done
     */

     AVLNode *successor = node->right;

     while (successor->left) {
        successor = successor->left; // go to the extreme left to find the successor of the current node
     }

     //detach the successor node from its postion
     AVLNode *root = avlDelOneChild(successor); //this function returns the root node (by perferming the height adusting bottom to top);

     //put the structural data of the node to the success node (swapping value in intrusive data structure)
    *successor = *node;

    // Copying the node's contents into the successor (intrusive replace).
    // We do NOT move or swap the actual node objects — we copy all fields from
    // `node` into the existing `successor` object. As a result:
    // - The successor's memory address stays the same (it is the same node object).
    // - The successor now holds the value, child pointers and metadata that
    //   previously belonged to `node`.
    // - We must update the parent pointers of successor's children below so
    //   those child nodes point back to `successor` and the tree remains valid.
     //reform the links
     if(successor->left) {
        successor->left->parent = successor;
     }
     if(successor->right) {
        successor->right->parent = successor;
     }

     // attach the successor to the parent, or update the root pointer
     AVLNode **from = &root; //hold the root node first
     AVLNode *parent = node->parent;

     if(parent) { //if parent exists then find the exact right or left side to link the successor
        from = parent->left == node ? &parent->left : &parent->right;
     }

     *from = successor; //*from may be the root node or the parents left or right
     return root;
}

/**
     *              D (rank=r+s+1)        B (rank=r)
                 ┌───┴───┐             ┌───┴───┐
        (rank=r) B       E             A       D (rank=r+s+1)
               ┌─┴─┐                         ┌─┴─┐
               A   C (size=s)       (size=s) C   E
 */
//avl offset funtion
AVLNode *avl_offset(AVLNode *node, int64_t offset) {
    int64_t pos = 0; //used to track the current rank of the node
    while (offset != pos) {

        if(pos < offset && pos + avlCnt(node->right) >= offset) { //means the node is present in the right subtree

            node = node->right;
            pos += avlCnt(node->left) + 1; //the pos/rank of the left node = rank of the parent + size of the left subtree + 1;
        } else if(pos > offset && pos - avlCnt(node->left) <= offset) { //if the node exists left side of the node

            node = node->left;
            pos -= avlCnt(node->right) + 1;
        } else { //if the node prest in the parent

            AVLNode *parent = node->parent;
            if(!parent) return NULL;

            //if the node was the left child
            if(parent->left == node) {
                pos += avlCnt(node->right) + 1;
            }
            //if the node was the right child
            if(parent->right == node) {
                pos -= avlCnt(node->left) + 1;
            }
            node = parent;

        }
    }
    return node;
}