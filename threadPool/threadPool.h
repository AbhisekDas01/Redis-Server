#pragma once

#include <stddef.h>
#include <pthread.h>
#include <vector>
#include <deque>

struct Work {
    void (*f)(void *) = NULL; //this is a generic function pointer that accepts exactly one argument of any type
    void *arg = NULL; //this is a generic data pointer which can point to any data type 
};

struct ThreadPool {
    std::vector<pthread_t> threads; //lists all the threads
    std::deque<Work> queue; //queue to store shared resourses
    pthread_mutex_t mu;
    pthread_cond_t not_empty;
    bool shutdown = false; // Flag to signal threads to stop
};


void threadPoolInit(ThreadPool *tp , size_t num_threads);
void threadPoolQueue(ThreadPool *tp , void (*f)(void *) , void *arg);
void threadPoolDestroy(ThreadPool *tp);