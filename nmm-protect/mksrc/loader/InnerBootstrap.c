#include "Bootstrap.h"
#include "BuildId.h"
#include <string.h>

extern jint nmmp_inner_on_load(JavaVM *vm, void *reserved);
const int *nmmp_private_failure_state;

__attribute__((visibility("default")))
int nmmp_inner_bootstrap_v1(JavaVM *vm, void *reserved, const NmmpHostV1 *host, NmmpInnerResultV1 *out) {
    if (!host || !out || host->abi_version != 1 || host->struct_size != sizeof(*host) ||
        out->abi_version != 1 || out->struct_size != sizeof(*out) || !host->outer_anchor ||
        !host->failure_state || memcmp(host->build_id, nmmp_build_id, 16)) return -1;
    nmmp_private_failure_state = host->failure_state;
    memcpy(out->build_id, nmmp_build_id, 16);
    out->jni_version = nmmp_inner_on_load(vm, reserved);
    out->error = out->jni_version == JNI_VERSION_1_6 ? 0 : -2;
    return out->error;
}
