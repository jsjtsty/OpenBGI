#include <jni.h>

#include <string>
#include <vector>

#include "openbgi/openbgi.h"

namespace {

jobjectArray strings(JNIEnv* env, const std::vector<std::string>& values) {
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray out = env->NewObjectArray(static_cast<jsize>(values.size()), cls, nullptr);
    for (size_t i = 0; i < values.size(); ++i) {
        jstring s = env->NewStringUTF(values[i].c_str());
        env->SetObjectArrayElement(out, static_cast<jsize>(i), s);
        env->DeleteLocalRef(s);
    }
    return out;
}

std::string utf(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(s, chars);
    return out;
}

} // namespace

extern "C" {

JNIEXPORT jstring JNICALL Java_org_openbgi_Engine_nativeVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(obgi_version());
}

// Returns null, or [id, name] of the game the folder holds.
JNIEXPORT jobjectArray JNICALL Java_org_openbgi_Engine_nativeDetect(JNIEnv* env, jclass, jstring dir) {
    obgi_game_info info{};
    if (!obgi_detect(utf(env, dir).c_str(), &info)) return nullptr;
    return strings(env, {info.id, info.name});
}

// Returns [ok, complete, arcsFound, hasScPatch, gameId, gameName, missing, message].
JNIEXPORT jobjectArray JNICALL Java_org_openbgi_Engine_nativeProbe(JNIEnv* env, jclass, jstring dir) {
    obgi_probe_result r{};
    obgi_probe(utf(env, dir).c_str(), &r);
    return strings(env, {
        std::to_string(r.ok),
        std::to_string(r.complete),
        std::to_string(r.arcs_found),
        std::to_string(r.has_sc_patch),
        r.game_id,
        r.game_name,
        r.missing,
        r.message,
    });
}

} // extern "C"
