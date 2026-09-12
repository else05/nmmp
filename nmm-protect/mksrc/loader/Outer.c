#include "Bootstrap.h"
#include "BuildId.h"
#include "Envelope.h"
#include "Loader.h"
#include "Once.h"
#include "vendor/monocypher/monocypher.h"
#include <android/log.h>
#include <android/api-level.h>
#include <string.h>
#include <time.h>

extern const unsigned char nmmp_payload[];
extern const size_t nmmp_payload_size;
extern const volatile unsigned char nmmp_key_share_a[32], nmmp_key_share_b[32];
static NmmpOnce load_once = NMMP_ONCE_INIT;
static NmmpModule *module;
static NmmpHostV1 host;
typedef struct { JavaVM *vm; void *reserved; } LoadArguments;

static uint64_t nanos(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;
}

static int initialize(void *opaque) {
    LoadArguments *args = opaque;
    int api = android_get_device_api_level();
    if (api != 26 && api != 27) return -1;
    JNIEnv *env = NULL;
    if ((*args->vm)->GetEnv(args->vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return -1;
    uint8_t key[32], *decoded = NULL;
    size_t decoded_size = 0;
    uint64_t start = nanos();
    for (unsigned i = 0; i < 32; ++i) key[i] = nmmp_key_share_a[i] ^ nmmp_key_share_b[i];
    int error = nmmp_open_payload(nmmp_payload, nmmp_payload_size, key, nmmp_build_id, &decoded, &decoded_size);
    crypto_wipe(key, sizeof(key));
    uint64_t unpacked = nanos();
    if (error) goto failed;
    error = nmmp_map_image(decoded, decoded_size, &module);
    nmmp_free_secret(decoded, decoded_size);
    decoded = NULL;
    if (error) goto failed;
    uint64_t mapped = nanos();
    host.abi_version = 1;
    host.struct_size = sizeof(host);
    memcpy(host.build_id, nmmp_build_id, 16);
    host.outer_anchor = &load_once;
    host.failure_state = &load_once.failed;
    error = nmmp_run_constructors(module);
    uint64_t constructed = nanos();
    if (error || __atomic_load_n(&load_once.failed, __ATOMIC_ACQUIRE)) goto failed;
    NmmpInnerResultV1 result = {1, sizeof(result), {0}, JNI_ERR, -1};
    error = ((NmmpBootstrapV1)nmmp_bootstrap_address(module))(args->vm, args->reserved, &host, &result);
    if (error || result.abi_version != 1 || result.struct_size != sizeof(result) || result.error ||
        result.jni_version != JNI_VERSION_1_6 || memcmp(result.build_id, nmmp_build_id, 16) ||
        (*env)->ExceptionCheck(env) || __atomic_load_n(&load_once.failed, __ATOMIC_ACQUIRE)) {
        error = -5;
        goto failed;
    }
    __android_log_print(ANDROID_LOG_INFO, "NMMP-Loader",
                        "ready id=%02x%02x%02x%02x bias=%p size=%zu unpack_us=%llu map_us=%llu constructors_us=%llu bootstrap_us=%llu",
                        nmmp_build_id[0], nmmp_build_id[1], nmmp_build_id[2], nmmp_build_id[3],
                        (void *)nmmp_image_bias(module), nmmp_image_size(module),
                        (unsigned long long)((unpacked - start) / 1000),
                        (unsigned long long)((mapped - unpacked) / 1000),
                        (unsigned long long)((constructed - mapped) / 1000),
                        (unsigned long long)((nanos() - constructed) / 1000));
    return 0;
failed:
    __atomic_store_n(&load_once.failed, 1, __ATOMIC_RELEASE);
    nmmp_free_secret(decoded, decoded_size);
    nmmp_discard_image(module); /* Does nothing after constructors have started. */
    __android_log_print(ANDROID_LOG_ERROR, "NMMP-Loader", "initialization failed (%d)", error);
    return -1;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    LoadArguments args = {vm, reserved};
    return nmmp_once(&load_once, initialize, &args) ? JNI_ERR : JNI_VERSION_1_6;
}
