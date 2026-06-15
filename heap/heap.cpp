#include "heap.h"


//function to get the index of the left-right child and the parent 
static size_t heapLeftChild(size_t index) {
    return index*2 + 1;
}

static size_t heapRightChild(size_t index) {
    return index*2 + 2;
}

static size_t heapParentIndex(size_t index) {
    return (index+1)/2 -1;
}

//function to move a node up if its greater than its parent
static void heapUp(HeapItem heap[] , size_t pos) {
    HeapItem t = heap[pos];
    while (pos > 0 && heap[heapParentIndex(pos)].val > t.val) {

        //swap the values with the parent
        heap[pos] = heap[heapParentIndex(pos)];
        *heap[pos].ref = pos; //update the new index of the swapped value to the Entry
        pos = heapParentIndex(pos); //move up to test other nodes
    }

    heap[pos] = t; //find a place to put the new node
    *heap[pos].ref = pos; //update the index in the Entry in server
}

//function to move down
static void heapDown(HeapItem heap[] , size_t pos , size_t len) {

    HeapItem t = heap[pos];

    while (true) {
        
        //find the smallest among the left and right index then swap the values 
        size_t leftIndex = heapLeftChild(pos);
        size_t rightIndex = heapRightChild(pos);
        size_t minPos = pos; //let current position as minimum 
        uint64_t minVal = t.val; //let current value is minimum 

        if(leftIndex < len && heap[leftIndex].val < minVal) {
            minPos = leftIndex;
            minVal = heap[leftIndex].val;
        }
        if(rightIndex < len && heap[rightIndex].val < minVal) {
            minPos = rightIndex;
        }

        if(minPos == pos) { //position did not change
            break;
        }

        //swap the values with its child
        heap[pos] = heap[minPos];
        *heap[pos].ref = pos;
        pos = minPos;
    }
    heap[pos] = t;
    *heap[pos].ref = pos;
}

void heapUpdate(HeapItem heap[] , size_t pos , size_t len) {

    if(pos > 0 && heap[heapParentIndex(pos)].val > heap[pos].val) {
        heapUp(heap , pos);
    } else {
        heapDown(heap , pos , len);
    }   
}

void heapDelete(std::vector<HeapItem> &heap , size_t pos) {
    
    heap[pos] = heap.back(); //insert the last value to the node to be deleted then pop the last index
    heap.pop_back();

    //heapify
    if(pos < heap.size()) {
        heapUpdate(heap.data() , pos , heap.size());
    }

}

void heapUpsert(std::vector<HeapItem> &heap, size_t pos, HeapItem t) {

    if(pos < heap.size()) {
        heap[pos] = t; //update the entry
    } else {
        //create a new record
        pos = heap.size();
        heap.push_back(t);
    }

    heapUpdate(heap.data() , pos , heap.size()); //update by heapify
}