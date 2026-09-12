#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <jni.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#define DEX_EDITOR_GLOBALCACHE_H
void cacheInitial(JNIEnv *env);
// Include the actual generated two-DEX initialization code, not a mirrored implementation.
#include NMMP_GENERATED_INIT

static unsigned entered, returned, success, bindings, prepared, registrations;
static int releaseBinding, releaseRegistration, mode;
static jboolean JNICALL exceptionCheck(JNIEnv *env) { (void)env; return false; }
static const struct JNINativeInterface_ functions = {.ExceptionCheck = exceptionCheck};
static JNIEnv environment = &functions;
void cacheInitial(JNIEnv *env) { (void)env; }
void classes_setup(JNIEnv *env) { (void)env; }
void classes2_setup(JNIEnv *env) { (void)env; }
bool vmBindingActivate(JNIEnv *env, jobject context) {
    (void)env; (void)context;
    __atomic_add_fetch(&bindings, 1, __ATOMIC_SEQ_CST);
    assert(!pthread_mutex_trylock(&gNmmpInit.mutex)); pthread_mutex_unlock(&gNmmpInit.mutex);
    while (!__atomic_load_n(&releaseBinding, __ATOMIC_SEQ_CST)) sched_yield();
    if (mode == 2) assert(!nmmp_vm_require_ready());
    return true;
}
bool classes_setup_activate(JNIEnv *env) { (void)env; __atomic_add_fetch(&prepared, 1, __ATOMIC_SEQ_CST); return true; }
bool classes2_setup_activate(JNIEnv *env) { return classes_setup_activate(env); }
bool classes_setup_finish(JNIEnv *env) {
    assert(__atomic_load_n(&prepared, __ATOMIC_SEQ_CST) == 2 && nmmp_vm_is_ready());
    assert(!pthread_mutex_trylock(&gNmmpRegister.mutex)); pthread_mutex_unlock(&gNmmpRegister.mutex);
    __atomic_add_fetch(&registrations, 1, __ATOMIC_SEQ_CST);
    // A legal class-initializer callback on the registering thread must not wait for itself.
    assert(nmmp_vm_activate(env, NULL) && nmmp_vm_require_ready());
    while (!__atomic_load_n(&releaseRegistration, __ATOMIC_SEQ_CST)) sched_yield();
    return mode != 1;
}
bool classes2_setup_finish(JNIEnv *env) { (void)env; return true; }
static void *activate(void *unused) {
    (void)unused;
    __atomic_add_fetch(&entered, 1, __ATOMIC_SEQ_CST);
    bool ready = nmmp_vm_activate(&environment, NULL);
    if (ready) __atomic_add_fetch(&success, 1, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&returned, 1, __ATOMIC_SEQ_CST);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2); mode = atoi(argv[1]);
    pthread_t threads[16];
    for (unsigned i = 0; i < 16; ++i) assert(!pthread_create(&threads[i], NULL, activate, NULL));
    while (__atomic_load_n(&entered, __ATOMIC_SEQ_CST) != 16) sched_yield();
    __atomic_store_n(&releaseBinding, 1, __ATOMIC_SEQ_CST);
    if (mode != 2) {
        while (!__atomic_load_n(&registrations, __ATOMIC_SEQ_CST)) sched_yield();
        assert(!__atomic_load_n(&returned, __ATOMIC_SEQ_CST));
        __atomic_store_n(&releaseRegistration, 1, __ATOMIC_SEQ_CST);
    }
    for (unsigned i = 0; i < 16; ++i) assert(!pthread_join(threads[i], NULL));
    assert(bindings == 1 && returned == 16 && prepared == 2);
    if (!mode) assert(success == 16 && registrations == 1 && nmmp_vm_is_ready());
    else {
        assert(!success && nmmp_vm_failed());
        assert(!nmmp_vm_activate(&environment, NULL) && bindings == 1);
    }
    printf("PASS: generated initialization mode=%d, 16 concurrent callers, no JNI-held mutex\n", mode);
}
