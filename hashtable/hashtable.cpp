#include <cassert>
#include <cstdlib>
#include "hashtable.h"

//initialize a fixed size hash table 
static void hInit(HTable *htab , size_t n) {
    assert(n > 0 && (n & (n-1)) == 0); //check if the size is > 0 && n must be a power of 2 (for faster module operation)
    htab->tab = (HNode **)calloc(n , sizeof(HNode *));  //allocate the n size block of memeory 
    htab->mask = n-1;
    htab->size = 0;
}


//insert function for the hash table
static void hInsert(HTable *htab , HNode *node) {

    size_t pos = node->hcode & htab->mask; //find the postion where to store the data (it is similar to (node->hcode % htab->mask));
    HNode *next = htab->tab[pos]; //extract the input bucket from that position
    //linked list insert at begin operation
    node->next = next; 
    htab->tab[pos] = node; 
    htab->size++; //increse the number of keys 
}

//lookup function for the hashmap 
static HNode **hLookUp(HTable *htab , HNode *key , bool (*eq)(HNode * , HNode*)) {
    
    if(!htab->tab) { //if the table is empty
        return NULL;
    }   

    size_t pos = key->hcode & htab->mask; //find the index of the hasedcode
    HNode **from = &htab->tab[pos];

    for(HNode *curr; (curr = *from) != NULL ; from = &curr->next) {

        /**
         * flow:
         * 1. Assign `*from` to `curr` and check if it's NULL (end of list).
         * 2. Check if the hash code and the key match the current node.
         * 3. If a match is found, return `from` (a pointer to the pointer pointing to `curr`).
         *    This double-pointer approach allows the caller to easily remove the node without a separate parent pointer.
         * 4. Move to the next node by updating `from` to point to `curr->next`.
         */
        if(curr->hcode == key->hcode && eq(curr , key)) {
            return from; 
        }
    }

    return NULL;
}

//delete operation
