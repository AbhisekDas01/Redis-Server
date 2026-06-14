#pragma once

#include <stddef.h>

struct DList {
    DList *prev = NULL;
    DList *next = NULL;
};

//function to detach a node
inline void dlistDetach(DList *node) {

    node->prev->next = node->next;
    node->next->prev = node->prev;
}

/**
 * NOTE: we are using a dummy node in circular manner to avoid dealing with the segmentation faults
 */
//function to create a dummy node to link itself(circular)
inline void dlistInit(DList *node) {
    node->prev = node->next = node;
}

//function to check if the node is empty
inline bool dlistEmpty(DList *node) {
    return node->next == node; //single dummy node exists
}

// insert the node to the end of the linked list
inline void dlistInsertBefore(DList *target , DList *newNode) {
    DList *prev = target->prev;
    prev->next = newNode;
    newNode->prev = prev;
    newNode->next = target;
    target->prev = newNode;
}