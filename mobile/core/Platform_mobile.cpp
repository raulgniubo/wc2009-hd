// melonDS::Platform for the mobile apps: plain C/C++ file I/O and threads; no Wi-Fi, camera or
// add-ons. Saves go through wc09_core.cpp.
#include "Platform.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace wc09m { void OnNDSSave(const melonDS::u8* data, melonDS::u32 len, melonDS::u32 off, melonDS::u32 wlen);
                  std::string LocalDir(); }

namespace melonDS::Platform
{

void SignalStop(StopReason, void*) {}

std::string GetLocalFilePath(const std::string& filename)
{
    if (!filename.empty() && filename[0] == '/') return filename;
    return wc09m::LocalDir() + "/" + filename;
}

static const char* ModeString(FileMode mode)
{
    if (mode & FileMode::Write)
    {
        if (mode & FileMode::Read) return (mode & FileMode::NoCreate) ? "r+b" : ((mode & FileMode::Preserve) ? "r+b" : "w+b");
        return (mode & FileMode::Preserve) ? "r+b" : "wb";
    }
    if (mode & FileMode::Append) return "ab";
    return "rb";
}

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    if ((mode & FileMode::ReadWrite) == FileMode::None) return nullptr;
    const char* m = ModeString(mode);
    FILE* f = fopen(path.c_str(), m);
    if (!f && (mode & FileMode::Write) && (mode & FileMode::Preserve) && !(mode & FileMode::NoCreate))
        f = fopen(path.c_str(), "w+b");   // preserve, but create when missing
    return (FileHandle*)f;
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return OpenFile(GetLocalFilePath(path), mode); }

bool FileExists(const std::string& name) { struct stat st; return stat(name.c_str(), &st) == 0; }
bool LocalFileExists(const std::string& name) { return FileExists(GetLocalFilePath(name)); }

bool CheckFileWritable(const std::string& filepath)
{
    FILE* f = fopen(filepath.c_str(), "ab");
    if (!f) return false;
    fclose(f);
    return true;
}
bool CheckLocalFileWritable(const std::string& filepath) { return CheckFileWritable(GetLocalFilePath(filepath)); }

bool CloseFile(FileHandle* file) { return fclose((FILE*)file) == 0; }
bool IsEndOfFile(FileHandle* file) { return feof((FILE*)file) != 0; }
bool FileReadLine(char* str, int count, FileHandle* file) { return fgets(str, count, (FILE*)file) != nullptr; }
u64 FilePosition(FileHandle* file) { return (u64)ftello((FILE*)file); }
bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    int o = origin == FileSeekOrigin::Start ? SEEK_SET : origin == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
    return fseeko((FILE*)file, offset, o) == 0;
}
void FileRewind(FileHandle* file) { rewind((FILE*)file); }
u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) { return fread(data, size, count, (FILE*)file); }
bool FileFlush(FileHandle* file) { return fflush((FILE*)file) == 0; }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) { return fwrite(data, size, count, (FILE*)file); }
u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    va_list args; va_start(args, fmt);
    int r = vfprintf((FILE*)file, fmt, args);
    va_end(args);
    return r < 0 ? 0 : (u64)r;
}
u64 FileLength(FileHandle* file)
{
    FILE* f = (FILE*)file;
    off_t pos = ftello(f); fseeko(f, 0, SEEK_END);
    off_t len = ftello(f); fseeko(f, pos, SEEK_SET);
    return (u64)len;
}

void Log(LogLevel level, const char* fmt, ...)
{
    if (level == LogLevel::Debug) return;
    va_list args; va_start(args, fmt);
#ifdef __ANDROID__
    __android_log_vprint(level == LogLevel::Error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "WC09", fmt, args);
#else
    vfprintf(stderr, fmt, args);
#endif
    va_end(args);
}

struct ThreadImpl { std::thread T; };
Thread* Thread_Create(std::function<void()> func) { auto* t = new ThreadImpl; t->T = std::thread(std::move(func)); return (Thread*)t; }
void Thread_Free(Thread* thread) { auto* t = (ThreadImpl*)thread; if (t->T.joinable()) t->T.detach(); delete t; }
void Thread_Wait(Thread* thread) { auto* t = (ThreadImpl*)thread; if (t->T.joinable()) t->T.join(); }

struct SemaImpl { std::mutex M; std::condition_variable CV; int Count = 0; };
Semaphore* Semaphore_Create() { return (Semaphore*)new SemaImpl; }
void Semaphore_Free(Semaphore* s) { delete (SemaImpl*)s; }
void Semaphore_Reset(Semaphore* s) { auto* x = (SemaImpl*)s; std::lock_guard<std::mutex> lk(x->M); x->Count = 0; }
void Semaphore_Wait(Semaphore* s)
{
    auto* x = (SemaImpl*)s; std::unique_lock<std::mutex> lk(x->M);
    x->CV.wait(lk, [x] { return x->Count > 0; }); x->Count--;
}
bool Semaphore_TryWait(Semaphore* s, int timeout_ms)
{
    auto* x = (SemaImpl*)s; std::unique_lock<std::mutex> lk(x->M);
    if (!x->CV.wait_for(lk, std::chrono::milliseconds(timeout_ms), [x] { return x->Count > 0; })) return false;
    x->Count--; return true;
}
void Semaphore_Post(Semaphore* s, int count)
{
    auto* x = (SemaImpl*)s;
    { std::lock_guard<std::mutex> lk(x->M); x->Count += count; }
    x->CV.notify_all();
}

Mutex* Mutex_Create() { return (Mutex*)new std::mutex; }
void Mutex_Free(Mutex* m) { delete (std::mutex*)m; }
void Mutex_Lock(Mutex* m) { ((std::mutex*)m)->lock(); }
void Mutex_Unlock(Mutex* m) { ((std::mutex*)m)->unlock(); }
bool Mutex_TryLock(Mutex* m) { return ((std::mutex*)m)->try_lock(); }

void Sleep(u64 usecs) { std::this_thread::sleep_for(std::chrono::microseconds(usecs)); }
u64 GetMSCount() { return (u64)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
u64 GetUSCount() { return (u64)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void*) { wc09m::OnNDSSave(savedata, savelen, writeoffset, writelen); }
void WriteGBASave(const u8*, u32, u32, u32, void*) {}
void WriteFirmware(const Firmware&, u32, u32, void*) {}
void WriteDateTime(int, int, int, int, int, int, void*) {}

void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }

void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32* frame, int width, int height, bool, void*) { memset(frame, 0, (size_t)width * height * 4); }

void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16* data, int maxlength, void*) { memset(data, 0, maxlength * sizeof(s16)); return maxlength; }

AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }

bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0.f; }

// used by the JIT on Android (libandroid's ASharedMemory_create)
DynamicLibrary* DynamicLibrary_Load(const char* lib) { return (DynamicLibrary*)dlopen(lib, RTLD_NOW | RTLD_LOCAL); }
void DynamicLibrary_Unload(DynamicLibrary* lib) { if (lib) dlclose(lib); }
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return lib ? dlsym(lib, name) : nullptr; }

}
