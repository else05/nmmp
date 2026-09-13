#include "Once.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static NmmpOnce concurrent = NMMP_ONCE_INIT, failed = NMMP_ONCE_INIT, reentry = NMMP_ONCE_INIT;
static int count, failures;
static void check(int ok) { if (!ok) abort(); }
static int initialize(void *context) {
    NmmpOnce *o = context;
    check(!pthread_mutex_trylock(&o->mutex));
    pthread_mutex_unlock(&o->mutex);
    __atomic_add_fetch(&count, 1, __ATOMIC_RELAXED);
    usleep(10000);
    return 0;
}
static int fail(void *context) { (void)context; ++failures; return -1; }
static int recursive(void *context) {
    check(nmmp_once(&reentry, recursive, context) == -1);
    return 0; /* A swallowed inner failure must still prevent READY. */
}
static void *worker(void *arg) { (void)arg; check(!nmmp_once(&concurrent, initialize, &concurrent)); return NULL; }
static int unexpected(void *context) { (void)context; abort(); }
static void *failed_worker(void *context) {
    check(nmmp_once(context, unexpected, NULL) == -1);
    return NULL;
}
static int recursive_waiters(void *context) {
    pthread_t threads[16];
    for (unsigned i = 0; i < 16; ++i)
        check(!pthread_create(&threads[i], NULL, failed_worker, context));
    check(nmmp_once(context, unexpected, NULL) == -1);
    /* Failure must wake waiters before this callback returns. */
    for (unsigned i = 0; i < 16; ++i) check(!pthread_join(threads[i], NULL));
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 2) {
        NmmpOnce state = NMMP_ONCE_INIT;
        if (!strcmp(argv[1], "reentry-waiters")) {
            check(nmmp_once(&state, recursive_waiters, &state) == -1);
        } else if (!strcmp(argv[1], "failure-before-init")) {
            __atomic_store_n(&state.failed, 1, __ATOMIC_RELEASE);
            check(nmmp_once(&state, unexpected, NULL) == -1);
        } else if (!strcmp(argv[1], "failure-after-ready")) {
            check(!nmmp_once(&state, initialize, &state));
            __atomic_store_n(&state.failed, 1, __ATOMIC_RELEASE);
            check(nmmp_once(&state, unexpected, NULL) == -1);
        } else abort();
        check(state.state == 3 && state.failed == 1);
        check(nmmp_once(&state, unexpected, NULL) == -1);
        pthread_cond_destroy(&state.changed);
        pthread_mutex_destroy(&state.mutex);
        puts("failure propagation passed");
        return 0;
    }
    pthread_t threads[16];
    for (unsigned i = 0; i < 16; ++i) check(!pthread_create(&threads[i], NULL, worker, NULL));
    for (unsigned i = 0; i < 16; ++i) check(!pthread_join(threads[i], NULL));
    check(count == 1 && concurrent.state == 2);
    for (unsigned i = 0; i < 3; ++i) check(nmmp_once(&failed, fail, NULL) == -1);
    check(failures == 1 && failed.failed == 1 && failed.state == 3);
    check(nmmp_once(&reentry, recursive, NULL) == -1 && reentry.state == 3);
    check(nmmp_once(&reentry, initialize, &reentry) == -1 && count == 1);
    puts("16-thread once, unlocked callbacks, permanent failure and same-thread reentry passed");
    return 0;
}
