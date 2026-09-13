#include "Bootstrap.h"
#include "BuildId.h"
#include "Envelope.h"
#include "Loader.h"
#include "Once.h"
#include "ProtectionManifestConfig.h"
#include "Stage0.h"
#include "vendor/monocypher/monocypher.h"
#include <android/log.h>
#include <android/api-level.h>
#include <string.h>
#include <time.h>

extern const unsigned char nmmp_payload[];
extern const size_t nmmp_payload_size;
extern const NmmpNativeProgram nmmp_stage0_programs[4];
static NmmpOnce load_once = NMMP_ONCE_INIT;
static NmmpModule *module;
static NmmpHostV1 host;
typedef struct { JavaVM *vm; void *reserved; } LoadArguments;

#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
static uint64_t nanos(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;
}
#endif

static int initialize(void *opaque) {
    LoadArguments *args = opaque;
    int api = android_get_device_api_level();
    if (api != 26 && api != 27) return -1;
    JNIEnv *env = NULL;
    if ((*args->vm)->GetEnv(args->vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return -1;
    uint8_t key[32] = {0}, *decoded = NULL;
    size_t decoded_size = 0;
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    uint64_t start = nanos();
#endif
    int error = -6;
    if (!nmmpRecoverStage0(nmmp_stage0_programs, nmmp_build_id, key)) goto failed;
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    uint64_t recovered = nanos();
#endif
    error = nmmp_open_payload(nmmp_payload, nmmp_payload_size, key, nmmp_build_id, &decoded, &decoded_size);
    crypto_wipe(key, sizeof(key));
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    uint64_t unpacked = nanos();
#endif
    if (error) goto failed;
    error = nmmp_map_image(decoded, decoded_size, &module);
    nmmp_free_secret(decoded, decoded_size);
    decoded = NULL;
    if (error) goto failed;
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    uint64_t mapped = nanos();
#endif
    host.abi_version = NMMP_PRIVATE_BOOTSTRAP_ABI;
    host.struct_size = sizeof(host);
    memcpy(host.build_id, nmmp_build_id, 16);
    memcpy(host.manifest_id, NMMP_PROTECTION_MANIFEST_ID, 16);
    host.outer_anchor = &load_once;
    host.failure_state = &load_once.failed;
    host.image_start = nmmp_image_start(module);
    host.image_size = nmmp_image_size(module);
    error = nmmp_run_constructors(module);
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    uint64_t constructed = nanos();
#endif
    if (error || __atomic_load_n(&load_once.failed, __ATOMIC_ACQUIRE)) goto failed;
    NmmpInnerResultV1 result = {NMMP_PRIVATE_BOOTSTRAP_ABI, sizeof(result), {0}, {0}, JNI_ERR, -1};
    error = ((NmmpBootstrapV1)nmmp_bootstrap_address(module))(args->vm, args->reserved, &host, &result);
    if (error || result.abi_version != NMMP_PRIVATE_BOOTSTRAP_ABI || result.struct_size != sizeof(result) || result.error ||
        result.jni_version != JNI_VERSION_1_6 || memcmp(result.build_id, nmmp_build_id, 16) ||
        memcmp(result.manifest_id, NMMP_PROTECTION_MANIFEST_ID, 16) ||
        (*env)->ExceptionCheck(env) || __atomic_load_n(&load_once.failed, __ATOMIC_ACQUIRE)) {
        error = -5;
        goto failed;
    }
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    __android_log_print(ANDROID_LOG_INFO, "NMMP-Loader",
                        "ready id=%02x%02x%02x%02x bias=%p size=%zu unpack_us=%llu map_us=%llu constructors_us=%llu bootstrap_us=%llu key_us=%llu",
                        nmmp_build_id[0], nmmp_build_id[1], nmmp_build_id[2], nmmp_build_id[3],
                        (void *)nmmp_image_bias(module), nmmp_image_size(module),
                        (unsigned long long)((unpacked - start) / 1000),
                        (unsigned long long)((mapped - unpacked) / 1000),
                        (unsigned long long)((constructed - mapped) / 1000),
                        (unsigned long long)((nanos() - constructed) / 1000),
                        (unsigned long long)((recovered - start) / 1000));
#endif
    return 0;
failed:
    crypto_wipe(key, sizeof(key));
    __atomic_store_n(&load_once.failed, 1, __ATOMIC_RELEASE);
    nmmp_free_secret(decoded, decoded_size);
    nmmp_discard_image(module); /* Does nothing after constructors have started. */
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    __android_log_print(ANDROID_LOG_ERROR, "NMMP-Loader", "initialization failed (%d)", error);
#else
    __android_log_print(ANDROID_LOG_ERROR, "NMMP-Loader", "initialization failed");
#endif
    return -1;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    LoadArguments args = {vm, reserved};
    return nmmp_once(&load_once, initialize, &args) ? JNI_ERR : JNI_VERSION_1_6;
}
