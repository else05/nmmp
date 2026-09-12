#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int nmmp_test_failure, nmmp_test_fail_at, nmmp_test_calls, nmmp_test_maps, nmmp_test_handles;
const char *nmmp_test_missing_name;
static int fail(int kind) { return nmmp_test_failure == kind && ++nmmp_test_calls == nmmp_test_fail_at; }
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t size) { return fail(1) ? NULL : __real_calloc(n, size); }
void *__real_mmap(void *, size_t, int, int, int, off_t);
void *__wrap_mmap(void *a, size_t n, int p, int f, int d, off_t o) {
    if (fail(2)) return MAP_FAILED;
    void *result = __real_mmap(a, n, p, f, d, o);
    if (result != MAP_FAILED) ++nmmp_test_maps;
    return result;
}
int __real_munmap(void *, size_t);
int __wrap_munmap(void *a, size_t n) {
    int result = __real_munmap(a, n);
    if (!result) --nmmp_test_maps;
    return result;
}
int __real_mprotect(void *, size_t, int);
int __wrap_mprotect(void *a, size_t n, int p) { return fail(3) ? -1 : __real_mprotect(a, n, p); }
void *__real_dlopen(const char *, int);
void *__wrap_dlopen(const char *name, int flags) {
    if (fail(4)) return NULL;
    void *handle = __real_dlopen(name, flags);
    if (handle) ++nmmp_test_handles;
    return handle;
}
int __real_dlclose(void *);
int __wrap_dlclose(void *h) {
    int result = __real_dlclose(h);
    if (!result) --nmmp_test_handles;
    return result;
}
void *__real_dlsym(void *, const char *);
void *__wrap_dlsym(void *h, const char *n) {
    if (nmmp_test_failure == 8 && nmmp_test_missing_name && !strcmp(n, nmmp_test_missing_name))
        return __real_dlsym(h, "__nmmp_test_missing_symbol_v1");
    return fail(5) ? NULL : __real_dlsym(h, n);
}
void *__real_dlvsym(void *, const char *, const char *);
void *__wrap_dlvsym(void *h, const char *n, const char *v) {
    if (nmmp_test_failure == 8 && nmmp_test_missing_name && !strcmp(n, nmmp_test_missing_name))
        return __real_dlvsym(h, "__nmmp_test_missing_symbol_v1", v);
    if (fail(6)) return NULL;
    void *result = __real_dlvsym(h, n, v);
    return fail(7) && result ? (char *)result + 1 : result;
}
