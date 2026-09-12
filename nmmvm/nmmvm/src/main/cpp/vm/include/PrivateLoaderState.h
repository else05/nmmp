#ifndef NMMP_PRIVATE_LOADER_STATE_H
#define NMMP_PRIVATE_LOADER_STATE_H
#if defined(NMMP_PRIVATE_LINKER)
#ifdef __cplusplus
extern "C" {
#endif
extern const int *nmmp_private_failure_state;
#ifdef __cplusplus
}
#endif
static inline int nmmpPrivateLoaderFailed(void) {
    return nmmp_private_failure_state == 0 ||
           __atomic_load_n(nmmp_private_failure_state, __ATOMIC_ACQUIRE);
}
#endif
#endif
