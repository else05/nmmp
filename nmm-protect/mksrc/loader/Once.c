#include "Once.h"

int nmmp_once(NmmpOnce *o, int (*initialize)(void *), void *context) {
    pthread_mutex_lock(&o->mutex);
    if (__atomic_load_n(&o->failed, __ATOMIC_ACQUIRE)) {
        o->state = 3;
        pthread_cond_broadcast(&o->changed);
        pthread_mutex_unlock(&o->mutex);
        return -1;
    }
    while (o->state == 1) {
        if (pthread_equal(o->owner, pthread_self())) {
            __atomic_store_n(&o->failed, 1, __ATOMIC_RELEASE);
            o->state = 3;
            pthread_cond_broadcast(&o->changed);
            pthread_mutex_unlock(&o->mutex);
            return -1;
        }
        pthread_cond_wait(&o->changed, &o->mutex);
    }
    if (o->state == 2 || o->state == 3) {
        int result = o->state == 2 ? 0 : -1;
        pthread_mutex_unlock(&o->mutex);
        return result;
    }
    o->state = 1;
    o->owner = pthread_self();
    pthread_mutex_unlock(&o->mutex);
    int result = initialize(context);
    pthread_mutex_lock(&o->mutex);
    if (result || __atomic_load_n(&o->failed, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&o->failed, 1, __ATOMIC_RELEASE);
        o->state = 3;
        result = -1;
    } else {
        o->state = 2;
    }
    pthread_cond_broadcast(&o->changed);
    pthread_mutex_unlock(&o->mutex);
    return result;
}
