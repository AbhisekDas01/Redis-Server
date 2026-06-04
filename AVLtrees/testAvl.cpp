#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <set>

#include "avl.h"

#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})

struct Data {
    AVLNode node;
    uint32_t val = 0;
};

struct Container {
    AVLNode *root = NULL;
};

static void add(Container &c , uint32_t val) {

    Data *data = new Data();
    avlInit(&data->node);
    data->val = val;

    AVLNode *curr = NULL;
    AVLNode **from = &c.root; // the incoming pointer to the next node
    while(*from) { //find a place to store the new node

        curr = *from; //extract the current node
        uint32_t nodeVal = container_of(curr, Data, node)->val;
        from = (val < nodeVal) ? &curr->left : &curr->right;
    }

    *from = &data->node; //store the node
    data->node.parent = curr;
    c.root = avlFix(&data->node);
}

static bool del(Container &c , uint32_t val) {

    AVLNode *curr = c.root;

    //find the node to delete
    while(curr) {
        uint32_t nodeVal = container_of(curr , Data ,node)->val;

        if(val == nodeVal) {
            break;
        }

        curr = val < nodeVal? curr->left : curr->right;
    }

    if(!curr) {
        return false; //node not found
    }

    c.root = avlDel(curr);
    delete container_of(curr , Data ,node);
    return true;
}

static void avlVerify(AVLNode *parent , AVLNode *node) {

    if(!node) return;

    assert(node->parent == parent); //check for the parent node relationship
    avlVerify(node , node->left); //also check the left and right subtree
    avlVerify(node , node->right);

    //check the count for the current node 
    assert(node->cnt == 1 + avlCnt(node->left) + avlCnt(node->right));

    // now check the height of the node 
    uint32_t l = avlHeight(node->left);
    uint32_t r = avlHeight(node->right);

    assert(l == r || l == r +1 || l+1 == r); //check the height is balanced or not 
    assert(node->height == std::max(l , r) + 1);

    //check the parent node value relationship
    uint32_t val = container_of(node , Data , node)->val;

    if(node->left) {
        assert(node->left->parent == node);
        assert(container_of(node->left , Data , node)->val <= val);
    }
    if(node->right) {
        assert(node->right->parent == node);
        assert(container_of(node->right , Data , node)->val >= val);
    }
}

// function to extract the data from the AVL tree
static void extract(AVLNode *node , std::multiset<uint32_t> &extracted) {

    if(!node) {
        return;
    }

    // inorder insertion 
    extract(node->left , extracted); 
    extracted.insert(container_of(node , Data , node)->val);
    extract(node->right , extracted);
}

//function to verify the container 
static void verifyContainer(Container &c , std::multiset<uint32_t> &ref) {

    avlVerify(NULL , c.root);
    assert(avlCnt(c.root) == ref.size());

    std::multiset<uint32_t> extracted;
    extract(c.root , extracted);

    assert(extracted == ref);
}

//function to dispose the tree
static void disponse(Container &c) {

    while(c.root) {
        AVLNode *root = c.root;
        c.root = avlDel(c.root);

        delete container_of(root , Data , node);
    }   
}


static void testInsert(uint32_t size) {


    for(uint32_t val = 0 ; val < size ; val++) {

        Container c;
        std::multiset<uint32_t> ref;
        
        //prefill some data to test the insert
        for(uint32_t i = 0 ; i < size ; i++) {

            if(i == val) { //skip the duplicate value
                continue;
            }

            add(c , i);
            ref.insert(i);
        }

        verifyContainer(c ,ref); //verify before new data insert
        add(c , val);
        ref.insert(val);
        verifyContainer(c , ref);
        disponse(c);
    }
}

static void testInsertDublicate(uint32_t size) {


    for(uint32_t val = 0 ; val < size ; val++) {

        Container c;
        std::multiset<uint32_t> ref;
        
        //prefill some data to test the insert
        for(uint32_t i = 0 ; i < size ; i++) {
            add(c , i);
            ref.insert(i);
        }

        verifyContainer(c ,ref); //verify before new data insert
        add(c , val);
        ref.insert(val);
        verifyContainer(c , ref);
        disponse(c);
    }
}

static void testRemove(uint32_t size) {

    for(uint32_t val = 0 ; val < size ; val++) {

        Container c;
        std::multiset<uint32_t> ref;
        
        //prefill some data to test the insert
        for(uint32_t i = 0 ; i < size ; i++) {
            add(c , i);
            ref.insert(i);
        }

        verifyContainer(c ,ref); //verify before new data delete
        assert(del(c , val));
        ref.erase(val);
        verifyContainer(c , ref); //verify after delete
        disponse(c);
    }

}


int main() {

    Container c;
    std::multiset<uint32_t> ref;
    //small tests
    verifyContainer(c , ref);
    add(c , 12);
    ref.insert(12);
    verifyContainer(c , ref);
    assert(!del(c , 200));
    assert(del(c , 12));
    ref.erase(12);
    verifyContainer(c , ref);

    //sequential insert
    for(uint32_t i = 0 ; i < 1000 ; i++) {
        add(c , i);
        ref.insert(i);
        verifyContainer(c , ref);
    }

    //random insert
    for(uint32_t i = 0 ; i < 100 ; i++) {
        uint32_t data = (uint32_t )rand() % 1000;
        add(c , data);
        ref.insert(data);
        verifyContainer(c , ref);
    }

    //random delete 
    for (uint32_t i = 0; i < 200; i++) {
    
        uint32_t data = (uint32_t)rand() % 1000;

        auto it = ref.find(data);
        if(it == ref.end()) {
            assert(!del(c , data));
        } else {
            assert(del(c , data));
            ref.erase(it);
        }

        verifyContainer(c , ref);
    }

    //insert and delete at various postions
    for(uint32_t i = 0 ; i < 200 ; i++) {
        testInsert(i);
        testInsertDublicate(i);
        testRemove(i);
    }
    
    disponse(c);
    return 0;
}