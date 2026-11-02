// jni_bridge.cpp - the Java/Kotlin side's view of engine_api.h:
// com.davidv.transcribe.engine.NativeEngine.
//
//   external fun catalog(): String
//   external fun transcribe(request: String, listener: EngineListener): String
//   external fun translate(request: String, listener: EngineListener): String
//   external fun cancel()          // the running job stops as soon as it can
//   external fun paragraphs(text: String): String
//
// EngineListener (Kotlin interface):
//   fun log(line: String)
//   fun progress(phase: String, done: Long, total: Long, unit: String)
//   fun modelFile(file: String, url: String, display: String, size: String): String  // throws if unavailable
#include <jni.h>

#include <atomic>
#include <stdexcept>
#include <string>

#include "engine_api.h"

namespace {

JavaVM* g_vm = nullptr;
std::atomic<bool> g_cancel{false};

// The JNIEnv of the calling thread (attaching it when a callback comes from a
// thread Java doesn't know).
JNIEnv* env_here() {
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
#ifdef __ANDROID__
        g_vm->AttachCurrentThread(&env, nullptr);
#else  // a desktop JDK's jni.h (the host build) declares it with void**
        g_vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr);
#endif
    return env;
}

std::string to_std(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

// Java's "modified UTF-8" differs from UTF-8 for characters outside the BMP
// (emoji, rare CJK), so strings go through byte arrays and String(bytes, UTF_8).
jstring to_java(JNIEnv* env, const std::string& s) {
    jbyteArray bytes = env->NewByteArray((jsize)s.size());
    env->SetByteArrayRegion(bytes, 0, (jsize)s.size(), reinterpret_cast<const jbyte*>(s.data()));
    jclass str = env->FindClass("java/lang/String");
    jmethodID ctor = env->GetMethodID(str, "<init>", "([BLjava/lang/String;)V");
    jstring charset = env->NewStringUTF("UTF-8");
    auto* r = static_cast<jstring>(env->NewObject(str, ctor, bytes, charset));
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(str);
    return r;
}

std::string from_java(JNIEnv* env, jstring s) {
    if (!s) return {};
    jclass str = env->FindClass("java/lang/String");
    jmethodID get = env->GetMethodID(str, "getBytes", "(Ljava/lang/String;)[B");
    jstring charset = env->NewStringUTF("UTF-8");
    auto* bytes = static_cast<jbyteArray>(env->CallObjectMethod(s, get, charset));
    std::string r;
    if (bytes) {
        const jsize n = env->GetArrayLength(bytes);
        r.resize((size_t)n);
        env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(r.data()));
        env->DeleteLocalRef(bytes);
    }
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(str);
    return r;
}

// A Java exception from a callback, as a C++ one (and cleared on the Java side).
void rethrow_java(JNIEnv* env) {
    if (!env->ExceptionCheck()) return;
    jthrowable ex = env->ExceptionOccurred();
    env->ExceptionClear();
    std::string msg = "the app reported an error";
    jclass cls = env->GetObjectClass(ex);
    jmethodID get = env->GetMethodID(cls, "getMessage", "()Ljava/lang/String;");
    if (get) {
        auto* m = static_cast<jstring>(env->CallObjectMethod(ex, get));
        if (env->ExceptionCheck()) env->ExceptionClear();
        else if (m) msg = from_java(env, m);
    }
    throw std::runtime_error(msg);
}

pkm::Callbacks callbacks(jobject listener) {
    pkm::Callbacks cb;
    cb.cancel = &g_cancel;
    cb.log = [listener](const std::string& line) {
        JNIEnv* env = env_here();
        jclass c = env->GetObjectClass(listener);
        jmethodID m = env->GetMethodID(c, "log", "(Ljava/lang/String;)V");
        jstring s = to_java(env, line);
        env->CallVoidMethod(listener, m, s);
        env->DeleteLocalRef(s);
        env->DeleteLocalRef(c);
        if (env->ExceptionCheck()) env->ExceptionClear();  // a log line never stops the job
    };
    cb.progress = [listener](const std::string& phase, long long done, long long total, const std::string& unit) {
        JNIEnv* env = env_here();
        jclass c = env->GetObjectClass(listener);
        jmethodID m = env->GetMethodID(c, "progress", "(Ljava/lang/String;JJLjava/lang/String;)V");
        jstring p = to_java(env, phase), u = to_java(env, unit);
        env->CallVoidMethod(listener, m, p, (jlong)done, (jlong)total, u);
        env->DeleteLocalRef(p);
        env->DeleteLocalRef(u);
        env->DeleteLocalRef(c);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };
    cb.model_file = [listener](const std::string& file, const std::string& url, const std::string& display,
                               const std::string& size) {
        JNIEnv* env = env_here();
        jclass c = env->GetObjectClass(listener);
        jmethodID m = env->GetMethodID(
            c, "modelFile", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        jstring a = to_java(env, file), b = to_java(env, url), d = to_java(env, display), s = to_java(env, size);
        auto* r = static_cast<jstring>(env->CallObjectMethod(listener, m, a, b, d, s));
        for (jobject o : {(jobject)a, (jobject)b, (jobject)d, (jobject)s, (jobject)c}) env->DeleteLocalRef(o);
        rethrow_java(env);
        if (!r) throw std::runtime_error(display + " could not be downloaded");
        std::string path = from_java(env, r);
        env->DeleteLocalRef(r);
        return path;
    };
    return cb;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL Java_com_davidv_transcribe_engine_NativeEngine_catalog(JNIEnv* env, jobject) {
    return to_java(env, pkm::catalog_json());
}

JNIEXPORT jstring JNICALL Java_com_davidv_transcribe_engine_NativeEngine_paragraphs(JNIEnv* env, jobject, jstring text) {
    return to_java(env, pkm::paragraphs(from_java(env, text)));
}

JNIEXPORT jstring JNICALL Java_com_davidv_transcribe_engine_NativeEngine_transcribe(JNIEnv* env, jobject,
                                                                                     jstring request, jobject listener) {
    g_cancel = false;
    // A global reference: callbacks may come from threads other than this one.
    jobject l = env->NewGlobalRef(listener);
    const std::string result = pkm::transcribe(from_java(env, request), callbacks(l));
    env->DeleteGlobalRef(l);
    return to_java(env, result);
}

JNIEXPORT jstring JNICALL Java_com_davidv_transcribe_engine_NativeEngine_translate(JNIEnv* env, jobject,
                                                                                    jstring request, jobject listener) {
    g_cancel = false;
    jobject l = env->NewGlobalRef(listener);
    const std::string result = pkm::translate(from_java(env, request), callbacks(l));
    env->DeleteGlobalRef(l);
    return to_java(env, result);
}

JNIEXPORT void JNICALL Java_com_davidv_transcribe_engine_NativeEngine_cancel(JNIEnv*, jobject) { g_cancel = true; }

}  // extern "C"
