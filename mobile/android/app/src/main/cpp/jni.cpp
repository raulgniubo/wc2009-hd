// JNI bridge between the Android app (io.github.wc2009hd.Native) and the WC2009 HD core, plus the
// AAudio output stream.
#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>
#include "wc09_core.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "WC09", __VA_ARGS__)

namespace
{
std::string JStr(JNIEnv* env, jstring s)
{
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

std::mutex StreamLock;
AAudioStream* Stream = nullptr;
short LastSample[2] = {0, 0};
std::atomic<bool> Muted{false};

aaudio_data_callback_result_t AudioCallback(AAudioStream*, void*, void* data, int32_t frames)
{
    short* out = (short*)data;
    int got = wc09_audio_read(out, frames);
    if (Muted) { memset(out, 0, (size_t)frames * 4); LastSample[0] = LastSample[1] = 0; return AAUDIO_CALLBACK_RESULT_CONTINUE; }
    if (got > 0) { LastSample[0] = out[got * 2 - 2]; LastSample[1] = out[got * 2 - 1]; }
    // not enough yet: fade the last sample out instead of clicking to zero
    for (int i = got; i < frames; i++)
    {
        LastSample[0] = (short)(LastSample[0] * 15 / 16); LastSample[1] = (short)(LastSample[1] * 15 / 16);
        out[i * 2] = LastSample[0]; out[i * 2 + 1] = LastSample[1];
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void ErrorCallback(AAudioStream*, void*, aaudio_result_t err);

void OpenStream()
{
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(b, AudioCallback, nullptr);
    AAudioStreamBuilder_setErrorCallback(b, ErrorCallback, nullptr);
    AAudioStream* s = nullptr;
    if (AAudioStreamBuilder_openStream(b, &s) == AAUDIO_OK)
    {
        Stream = s;
        AAudioStream_requestStart(s);
        LOGI("audio: %d Hz, burst %d", AAudioStream_getSampleRate(s), AAudioStream_getFramesPerBurst(s));
    }
    AAudioStreamBuilder_delete(b);
}

void CloseStream()
{
    if (!Stream) return;
    AAudioStream_requestStop(Stream);
    AAudioStream_close(Stream);
    Stream = nullptr;
}

// headphones unplugged etc.: the stream is dead, open a new one (not from the callback thread)
void ErrorCallback(AAudioStream* s, void*, aaudio_result_t err)
{
    if (err != AAUDIO_ERROR_DISCONNECTED) return;
    std::thread([s] {
        std::lock_guard<std::mutex> lk(StreamLock);
        if (Stream != s) return;
        CloseStream();
        OpenStream();
    }).detach();
}
}

#define FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_io_github_wc2009hd_Native_##name

FN(jint, init)(JNIEnv* env, jclass, jstring game, jstring data, jstring fonts)
{
    return wc09_init(JStr(env, game).c_str(), JStr(env, data).c_str(), JStr(env, fonts).c_str());
}
FN(jstring, findRom)(JNIEnv* env, jclass) { return env->NewStringUTF(wc09_find_rom()); }
FN(jint, loadRom)(JNIEnv* env, jclass, jstring path, jint scale, jboolean jit)
{
    return wc09_load_rom(JStr(env, path).c_str(), scale, jit ? 1 : 0);
}
FN(void, setScale)(JNIEnv*, jclass, jint s) { wc09_set_scale(s); }
FN(void, runFrame)(JNIEnv*, jclass) { wc09_run_frame(); }
FN(void, drawScreen)(JNIEnv*, jclass, jint screen, jint x, jint y, jint w, jint h, jint fbw, jint fbh)
{
    wc09_draw_screen(screen, x, y, w, h, fbw, fbh);
}
FN(void, setKeys)(JNIEnv*, jclass, jint keys) { wc09_set_keys((unsigned)keys); }
FN(void, touch)(JNIEnv*, jclass, jboolean down, jint x, jint y) { wc09_touch(down ? 1 : 0, x, y); }
FN(jint, saveState)(JNIEnv*, jclass, jint slot) { return wc09_save_state(slot); }
FN(jint, loadState)(JNIEnv*, jclass, jint slot) { return wc09_load_state(slot); }
FN(jboolean, stateExists)(JNIEnv*, jclass, jint slot) { return wc09_state_exists(slot) ? JNI_TRUE : JNI_FALSE; }
FN(jint, deleteState)(JNIEnv*, jclass, jint slot) { return wc09_delete_state(slot); }
FN(void, flushSave)(JNIEnv*, jclass) { wc09_flush_save(); }
FN(void, glReset)(JNIEnv*, jclass) { wc09_gl_reset(); }
FN(jint, audioQueued)(JNIEnv*, jclass) { return wc09_audio_queued(); }
FN(void, audioStart)(JNIEnv*, jclass) { std::lock_guard<std::mutex> lk(StreamLock); if (!Stream) OpenStream(); }
FN(void, audioStop)(JNIEnv*, jclass) { std::lock_guard<std::mutex> lk(StreamLock); CloseStream(); }
FN(void, setMuted)(JNIEnv*, jclass, jboolean m) { Muted = m; }
FN(void, shutdown)(JNIEnv*, jclass) { wc09_shutdown(); }
FN(void, setOption)(JNIEnv*, jclass, jint o, jint v) { wc09_set_option(o, v); }
FN(jstring, romInfo)(JNIEnv* env, jclass, jstring path, jintArray argb)
{
    char title[512];
    unsigned char px[32 * 32 * 4];
    if (wc09_rom_info(JStr(env, path).c_str(), title, sizeof(title), px) != 0) return nullptr;
    if (argb && env->GetArrayLength(argb) >= 32 * 32)
    {
        jint out[32 * 32];
        for (int i = 0; i < 32 * 32; i++)
            out[i] = (jint)((unsigned)px[i * 4 + 3] << 24 | (unsigned)px[i * 4] << 16 | (unsigned)px[i * 4 + 1] << 8 | px[i * 4 + 2]);
        env->SetIntArrayRegion(argb, 0, 32 * 32, out);
    }
    return env->NewStringUTF(title);
}
