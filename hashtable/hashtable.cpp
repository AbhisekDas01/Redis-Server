#include <cassert>
#include <cstdlib>
#include "hashtable.h"

//initialize a fixed size hash table 
// See explanation: hashtable.md#d-bitwise-masking-vs-modulo
static void hInit(HTable *htab , size_t n) {
    assert(n > 0 && (n & (n-1)) == 0); //check if the size is > 0 && n must be a power of 2 (for faster module operation)
    htab->tab = (HNode **)calloc(n , sizeof(HNode *));  //allocate the n size block of memeory (Create a array of HNode * of n size) -> HNode ** -> its a pointer to pointer
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
// See explanation: hashtable.md#c-the-double-pointer-hnode-list-traversal-technique
/**
 * HTable *htab -> the hash table we are looking to extract the data
 * HNode *key -> the dummy node created by using the hashfuction to match the index
 * bool (*eq)(Node * , Node*) => its a pointer to a function which hash the signature as [bool fun(Node * , Node *)]
 */
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

//delete operation (not actual delete)
static HNode *hDetach(HTable *htab , HNode **from) {

    HNode *node = *from; //get the target node
    *from = node->next; //assign the next node to the from to detach the current node
    htab->size--;
    return node;
}


//Progressive rehashing
// See explanation: hashtable.md#incremental-garbage-collection
const size_t k_rehashing_work = 128;    // constant work

static void hmHelpRehashint(HMap *hmap) {
    size_t nwork = 0;

    while(nwork < k_rehashing_work && hmap->older.size > 0) {

        //find a non empty slot to migrate
        HNode **from = &hmap->older.tab[hmap->migratePos];
        if(!from) { //if the slot is empty then move to next slot
            hmap->migratePos++;
            continue;
        }

        //if the slot if found then detact one node from the older and transfer to the newer node
        HNode *node = hDetach(&hmap->older , from);
        hInsert(&hmap->newer , node);
        nwork++;
    }
    
    // discard the old table if done
    if(hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HTable{};
    }
}

//HashMap rehashing new table allocation
static void hmTriggerRehashing(HMap *hmap) {

    hmap->older = hmap->newer; //make the current table as old one 
    hInit(&hmap->newer , (hmap->newer.mask + 1)*2); //assign a double size array
    hmap->migratePos = 0;
}

//HashMap lookUp operation
/**
 * As we have both the tables older and newer then we will search 
 * newer first if not found then search older (To get the data during the rehashing process)
 */

 HNode *hmLookup(HMap *hmap , HNode *key , bool(*eq)(HNode * , HNode *)) {

    hmHelpRehashint(hmap); //help transfering some portion of keys from the older table to newer one

    //1.find the key int the  newer table
    HNode **from = hLookUp(&hmap->newer , key , eq);

    if(!from) { //if the data is not found in the newer address
        from = hLookUp(&hmap->older , key , eq);
    }

    return from ? *from : NULL; //return the node
 } 

//HashMap delete function
/**
* first find the key in the newer table if founc the call detach
* else find the older table
*/

HNode *hmDelete(HMap *hmap , HNode *key , bool (*eq)(HNode * , HNode*)) {
    hmHelpRehashint(hmap); //help transfering some portion of keys from the older table to newer one

    if(HNode **from = hLookUp(&hmap->newer , key , eq)) {
        return hDetach(&hmap->newer , from);
    }

    if(HNode **from = hLookUp(&hmap->older , key , eq)) {
        return hDetach(&hmap->older , from);
    }

    return NULL;
}


/*HashMap insert function*/
/**
 * 1.Allocate the memory to the newer table if it is not initialzied
 * 2.Insert the data to the new table
 * 3.Check if we need to  rehash by checking the loadfactor
 * finally: help to migrate some data to the newer table (it will be done in a phase wise);
 */

const size_t k_max_load_factor = 8; //maximum keys we can store in a single chain

 void hmInsert(HMap *hmap , HNode *node) {
    if(!hmap->newer.tab) { //if no data is allocated the allocate the memory
        hInit(&hmap->newer , 4); //initialize with size 4
    }

    hInsert(&hmap->newer , node); //insert the data to the table

    //check if the rehashing is needed (if the data exceeds the load factor limit)
    if(!hmap->older.tab) { //if any existing older table dont exist then check for the load factor (the rehashing process may already running )

        size_t threshold = (hmap->newer.mask+1) * k_max_load_factor; //maximum data we can store in the table
        
        if(hmap->newer.size >= threshold) { //if the size exceed the threshold then we need to rehash

            hmTriggerRehashing(hmap);
        }
    }

    hmHelpRehashint(hmap); //help transfering some portion of keys from the older table to newer one

 }

 size_t hmSize(HMap *hmap) {
    return hmap->newer.size + hmap->older.size;
 }

 static bool hForEach(HTable *htab , bool (*f)(HNode * , void *) , void *arg) {
    for(size_t i = 0 ; htab->mask != 0 && i <=  htab->mask ; i++ ) { //go to each index

        for(HNode *node = htab->tab[i]; node != NULL ; node = node->next) {
            if(!f(node , arg)) {
                return false;
            }
        }
        
    }
    return true;
 }

 void   hmForeach(HMap *hmap, bool (*f)(HNode *, void *), void *arg) {

    hForEach(&hmap->newer , f , arg) && hForEach(&hmap->older , f , arg);
 }