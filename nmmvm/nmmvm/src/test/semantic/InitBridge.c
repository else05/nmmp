#include <jni.h>
#include <stdbool.h>
#include <sched.h>
#define DEX_EDITOR_GLOBALCACHE_H
void cacheInitial(JNIEnv *env);
#define JNI_OnLoad nmmp_generated_on_load
#include NMMP_GENERATED_INIT
#undef JNI_OnLoad

static int mode, release;
static unsigned bindings;
void cacheInitial(JNIEnv *env) { (void)env; }
void classes_setup(JNIEnv *env) { (void)env; }
void classes2_setup(JNIEnv *env) { (void)env; }
static bool callback(JNIEnv *env, const char *name) {
    jclass cls = (*env)->FindClass(env, "com/nmmedit/semantic/InitMain");
    if (!cls) return false;
    jmethodID method = (*env)->GetStaticMethodID(env, cls, name, "()V");
    if (method) (*env)->CallStaticVoidMethod(env, cls, method);
    (*env)->DeleteLocalRef(env, cls);
    return method && !(*env)->ExceptionCheck(env);
}
bool vmBindingActivate(JNIEnv *env, jobject context) {
    (void)context;
    __atomic_add_fetch(&bindings, 1, __ATOMIC_SEQ_CST);
    while (!__atomic_load_n(&release, __ATOMIC_SEQ_CST)) sched_yield();
    return mode != 2 || callback(env, "earlyCallback");
}
bool classes_setup_activate(JNIEnv *env) { (void)env; return true; }
bool classes2_setup_activate(JNIEnv *env) { (void)env; return true; }
bool classes_setup_finish(JNIEnv *env) { return callback(env, "registrationCallback") && mode != 1; }
bool classes2_setup_finish(JNIEnv *env) { (void)env; return true; }
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) { (void)vm; (void)reserved; return JNI_VERSION_1_6; }
JNIEXPORT void JNICALL Java_com_nmmedit_semantic_InitMain_configure(JNIEnv *env, jclass cls, jint value) { (void)env; (void)cls; mode = value; }
JNIEXPORT jboolean JNICALL Java_com_nmmedit_semantic_InitMain_activate(JNIEnv *env, jclass cls) { (void)cls; return nmmp_vm_activate(env, NULL); }
JNIEXPORT jboolean JNICALL Java_com_nmmedit_semantic_InitMain_requireReady(JNIEnv *env, jclass cls) { (void)env; (void)cls; return nmmp_vm_require_ready(); }
JNIEXPORT void JNICALL Java_com_nmmedit_semantic_InitMain_releaseBinding(JNIEnv *env, jclass cls) { (void)env; (void)cls; __atomic_store_n(&release, 1, __ATOMIC_SEQ_CST); }
JNIEXPORT jint JNICALL Java_com_nmmedit_semantic_InitMain_bindingCalls(JNIEnv *env, jclass cls) { (void)env; (void)cls; return __atomic_load_n(&bindings, __ATOMIC_SEQ_CST); }
