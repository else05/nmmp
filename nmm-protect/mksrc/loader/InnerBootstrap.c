#include "Bootstrap.h"
#include "BuildId.h"
#include "ProtectionManifestConfig.h"
#include "PrivateLoaderState.h"
#include <string.h>
#include <stdint.h>

extern jint nmmp_inner_on_load(JavaVM *vm, void *reserved);
const int *nmmp_private_failure_state;
const void *nmmp_private_image_start;
size_t nmmp_private_image_size;

__attribute__((visibility("default")))
int nmmp_inner_bootstrap_v1(JavaVM *vm, void *reserved, const NmmpHostV1 *host, NmmpInnerResultV1 *out) {
    if (!host || !out || host->abi_version != NMMP_PRIVATE_BOOTSTRAP_ABI || host->struct_size != sizeof(*host) ||
        out->abi_version != NMMP_PRIVATE_BOOTSTRAP_ABI || out->struct_size != sizeof(*out) || !host->outer_anchor ||
        !host->failure_state || !host->image_start || !host->image_size ||
        memcmp(host->build_id, nmmp_build_id, 16) ||
        memcmp(host->manifest_id, NMMP_PROTECTION_MANIFEST_ID, 16)) return -1;
    const uintptr_t entry = (uintptr_t)&nmmp_inner_bootstrap_v1;
    const uintptr_t start = (uintptr_t)host->image_start;
    if (entry < start || entry - start >= host->image_size) return -1;
    nmmp_private_failure_state = host->failure_state;
    nmmp_private_image_start = host->image_start;
    nmmp_private_image_size = host->image_size;
    memcpy(out->build_id, nmmp_build_id, 16);
    memcpy(out->manifest_id, NMMP_PROTECTION_MANIFEST_ID, 16);
    out->jni_version = nmmp_inner_on_load(vm, reserved);
    out->error = out->jni_version == JNI_VERSION_1_6 ? 0 : -2;
    return out->error;
}
