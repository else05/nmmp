#ifndef NMMP_PRIVATE_ONCE_H
#define NMMP_PRIVATE_ONCE_H
#include <pthread.h>
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t owner;
    int state;
    int failed;
} NmmpOnce;
#define NMMP_ONCE_INIT {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, (pthread_t)0, 0, 0}
/* Reentry permanently fails and wakes waiters before the owner callback returns. */
int nmmp_once(NmmpOnce *once, int (*initialize)(void *), void *context);
#endif
