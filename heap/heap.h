#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

struct HeapItem {
    uint64_t val; // heap value, the expiration time
    size_t *ref = NULL; // points to `Entry::heap_idx` to maintain the intrusive nature 
};

void heapUpdate(HeapItem heap[] , size_t pos , size_t len);
void heapDelete(std::vector<HeapItem> &heap , size_t pos);
void heapUpsert(std::vector<HeapItem> &heap, size_t pos, HeapItem t);