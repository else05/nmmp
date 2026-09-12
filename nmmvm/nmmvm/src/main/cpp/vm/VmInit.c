#include "VmInit.h"

bool vmInitIsOwner(VmInit *c) {
    pthread_mutex_lock(&c->mutex);
    bool owner = c->state == 1 && pthread_equal(c->owner, pthread_self());
    pthread_mutex_unlock(&c->mutex);
    return owner;
}

void vmInitFail(VmInit *c) {
    pthread_mutex_lock(&c->mutex);
    __atomic_store_n(&c->state, 3, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&c->condition);
    pthread_mutex_unlock(&c->mutex);
}

bool vmInitRequireReady(VmInit *c) {
    if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) == 2) return true;
    pthread_mutex_lock(&c->mutex);
    bool ready = c->state == 2;
    if (c->state == 1 && pthread_equal(c->owner, pthread_self())) {
        __atomic_store_n(&c->state, 3, __ATOMIC_RELEASE);
        pthread_cond_broadcast(&c->condition);
    }
    pthread_mutex_unlock(&c->mutex);
    return ready;
}

bool vmInitRun(VmInit *c, bool (*initialize)(void *), void *argument) {
    if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) == 2) return true;
    pthread_mutex_lock(&c->mutex);
    while (c->state == 1) {
        if (pthread_equal(c->owner, pthread_self())) {
            __atomic_store_n(&c->state, 3, __ATOMIC_RELEASE);
            pthread_cond_broadcast(&c->condition);
            pthread_mutex_unlock(&c->mutex);
            return false;
        }
        pthread_cond_wait(&c->condition, &c->mutex);
    }
    if (c->state) {
        bool ready = c->state == 2;
        pthread_mutex_unlock(&c->mutex);
        return ready;
    }
    c->owner = pthread_self();
    __atomic_store_n(&c->state, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&c->mutex);
    bool ready = initialize(argument);
    pthread_mutex_lock(&c->mutex);
    ready = ready && c->state == 1;
    __atomic_store_n(&c->state, ready ? 2 : 3, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&c->condition);
    pthread_mutex_unlock(&c->mutex);
    return ready;
}
