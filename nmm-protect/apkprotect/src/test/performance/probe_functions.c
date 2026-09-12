#include <jni.h>
#include <stdint.h>
#include <stddef.h>

/* Native allocations only. ART-managed objects are outside these link wrappers. */
static uint64_t counters[20];
static int enabled;
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t, size_t);
extern void *__real_realloc(void *, size_t);
extern void __real_free(void *);

static void maximum(unsigned index, uint64_t value) {
    uint64_t old = __atomic_load_n(&counters[index], __ATOMIC_RELAXED);
    while (old < value && !__atomic_compare_exchange_n(&counters[index], &old, value, 0,
                                                     __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
static void allocation(size_t bytes) {
    if (!__atomic_load_n(&enabled, __ATOMIC_RELAXED)) return;
    __atomic_fetch_add(&counters[0], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counters[1], bytes, __ATOMIC_RELAXED);
    maximum(2, bytes);
}
void *__wrap_malloc(size_t bytes) { allocation(bytes); return __real_malloc(bytes); }
void *__wrap_calloc(size_t count, size_t bytes) { allocation(count * bytes); return __real_calloc(count, bytes); }
void *__wrap_realloc(void *ptr, size_t bytes) { allocation(bytes); return __real_realloc(ptr, bytes); }
void __wrap_free(void *ptr) {
    if (ptr && __atomic_load_n(&enabled, __ATOMIC_RELAXED))
        __atomic_fetch_add(&counters[3], 1, __ATOMIC_RELAXED);
    __real_free(ptr);
}
void nmmpTestRead(uint32_t domain, uint32_t offset, int miss) {
    if (!__atomic_load_n(&enabled, __ATOMIC_RELAXED) || domain < 1 || domain > 5) return;
    __atomic_fetch_add(&counters[3 + domain], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counters[8 + domain], miss != 0, __ATOMIC_RELAXED);
    maximum(13 + domain, offset);
}
JNIEXPORT void JNICALL Java_bench_BenchProbe_reset(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    __atomic_store_n(&enabled, 0, __ATOMIC_RELAXED);
    for (unsigned i = 0; i < 20; ++i) __atomic_store_n(&counters[i], 0, __ATOMIC_RELAXED);
    __atomic_store_n(&enabled, 1, __ATOMIC_RELAXED);
}
JNIEXPORT jlongArray JNICALL Java_bench_BenchProbe_snapshot(JNIEnv *env, jclass cls) {
    (void)cls;
    __atomic_store_n(&enabled, 0, __ATOMIC_RELAXED);
    jlong values[20];
    for (unsigned i = 0; i < 20; ++i) values[i] = __atomic_load_n(&counters[i], __ATOMIC_RELAXED);
    jlongArray result = (*env)->NewLongArray(env, 20);
    if (result) (*env)->SetLongArrayRegion(env, result, 0, 20, values);
    return result;
}
