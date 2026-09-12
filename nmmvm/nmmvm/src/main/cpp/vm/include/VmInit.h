#ifndef NMMP_VM_INIT_H
#define NMMP_VM_INIT_H

#include <pthread.h>
#include <stdbool.h>

// The initializer runs outside the mutex. A failed initialization never retries.
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t owner;
    int state;
} VmInit;
#define NMMP_VM_INIT {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0}

#ifdef __cplusplus
extern "C" {
#endif
bool vmInitRun(VmInit *control, bool (*initialize)(void *), void *argument);
bool vmInitRequireReady(VmInit *control);
void vmInitFail(VmInit *control);
bool vmInitIsOwner(VmInit *control);
#ifdef __cplusplus
}
#endif
#endif
