#include "threadPool.h"
#include <assert.h>



/**
 *
void produce_1() {
    mu.lock();
    queue.push_back(work);
    cond.signal();  // signal before releasing the lock
    mu.unlock();
}
 */
void threadPoolQueue(ThreadPool *tp , void (*f)(void *) , void *arg) {

    pthread_mutex_lock(&tp->mu);
    tp->queue.push_back(Work {f , arg});
    pthread_cond_signal(&tp->not_empty);
    pthread_mutex_unlock(&tp->mu);

}

/**
 * void consume() {
    mu.lock();
    // wait for the queue to become non-empty
    while (queue.empty()) {
        // release the lock and enter sleep in a single step
        cond.wait(mu);
        // lock reclaimed
    }
    // update the queue
    Work work = queue.front();
    queue.pop_front();
    mu.unlock();
    // Do something with the work ...
}
 */

static void *worker(void *arg) {
    ThreadPool *tp = (ThreadPool *)arg;
    while (true) {
        pthread_mutex_lock(&tp->mu);
        // wait for the condition: a non-empty queue or shutdown
        while(tp->queue.empty() && !tp->shutdown) {
            pthread_cond_wait(&tp->not_empty , &tp->mu); //atomic operation 1.mutex-unlock 2.sleep 
            //after succesfully wakes then 3.relocks the mutex 
        }

        if (tp->queue.empty() && tp->shutdown) {
            pthread_mutex_unlock(&tp->mu);
            break;
        }

        Work work = tp->queue.front();
        tp->queue.pop_front();
        pthread_mutex_unlock(&tp->mu);
        work.f(work.arg); //run the task it is assigned to 
    }
    return NULL;
}

void threadPoolInit(ThreadPool *tp , size_t num_threads) {

    pthread_mutex_init(&tp->mu , NULL);
    pthread_cond_init(&tp->not_empty , NULL);
    tp->threads.resize(num_threads);
    for (size_t i = 0 ; i < num_threads ; i++) {
        int rv = pthread_create(&tp->threads[i] , NULL , &worker , tp);
        assert(rv == 0);
    }
}

void threadPoolDestroy(ThreadPool *tp) {

    pthread_mutex_lock(&tp->mu);
    tp->shutdown = true;
    pthread_cond_broadcast(&tp->not_empty); // Wake up all sleeping workers
    pthread_mutex_unlock(&tp->mu);

    for(size_t i = 0 ; i < tp->threads.size() ; i++) {
        pthread_join(tp->threads[i] , NULL);
    }
    pthread_mutex_destroy(&tp->mu);
    pthread_cond_destroy(&tp->not_empty);
}