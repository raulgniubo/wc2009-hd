// WC2009 HD core for the mobile apps: melonDS + the WC09 HD work, OpenGL ES renderer.
#include "wc09_core.h"

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "Portable.h"

#include "NDS.h"
#include "NDSCart.h"
#include "Args.h"
#include "SPU.h"
#include "GPU.h"
#include "GPU_OpenGL.h"
#include "OpenGLSupport.h"
#include "Savestate.h"
#include "SPI_Firmware.h"
#include "Platform.h"
#include "WC09Battle.h"
#include "WC09Perf.h"
#include "WC09Cards.h"
#include "WC09Render.h"

#ifndef MELONDS_GLES
// desktop OpenGL: the GL call counters only exist in the GLES wrapper
namespace melonDS::GLES
{
struct Stats { unsigned draws, bufUploads, bufBytes, texUploads, texBytes, readbacks, passBreaks; };
inline Stats& St() { static Stats s{}; return s; }
}
#endif
using namespace melonDS;

namespace wc09m
{
std::string GameDir, DataDir, FontDir, RomPath, RomBase, FoundRom;
std::unique_ptr<NDS> Nds;
int Scale = 4;
std::mutex AudioLock;

// in-game save: written to Saves/<rom>.sav a moment after the game stops writing
std::mutex SaveLock;
std::vector<u8> SaveData;
bool SaveDirty = false;
u64 SaveStamp = 0;

// audio: the emulator thread moves each frame's samples into this ring; the audio callback only
// takes this short lock, never the emulator's
std::mutex RingLock;
std::vector<s16> Ring(48000 * 2);   // 1 s stereo
size_t RingRead = 0, RingCount = 0; // in frames
const size_t kRingMax = 48000 / 8;  // keep at most 125 ms queued (drop the oldest beyond)

void PushAudio()
{
    static s16 tmp[4096 * 2];
    for (;;)
    {
        int n = Nds->SPU.ReadOutput(tmp, 4096);
        if (n <= 0) break;
        std::lock_guard<std::mutex> lk(RingLock);
        size_t cap = Ring.size() / 2;
        for (int i = 0; i < n; i++)
        {
            size_t w = (RingRead + RingCount) % cap;
            Ring[w * 2] = tmp[i * 2]; Ring[w * 2 + 1] = tmp[i * 2 + 1];
            if (RingCount < cap) RingCount++; else RingRead = (RingRead + 1) % cap;
        }
        if (RingCount > kRingMax) { RingRead = (RingRead + RingCount - kRingMax) % cap; RingCount = kRingMax; }
        if (n < 4096) break;
    }
}

std::string LocalDir() { return DataDir; }

// the last errors/warnings, for the "graphics problem" report
std::mutex LogLock;
std::string LogTail;
void NoteLog(bool error, const char* text)
{
    if (!error) return;
    std::lock_guard<std::mutex> lk(LogLock);
    if (LogTail.size() < 6000) LogTail += text;   // the first ones are the ones that matter
}
std::string Report;

u64 PerfFrames = 0, PerfUs = 0, PerfStart = 0, PerfMaxUs = 0;
char PerfText[256] = "";
int PerfFps = 0;
std::string PackDir;   // the HD pack in use ("" = none)
u64 PerfLastCall = 0, PerfMaxGapUs = 0, PerfRender0 = 0, PerfRender3D0 = 0, PerfUploads0 = 0;

// debug report: one sample per second, the last 15 minutes
struct PerfSample
{
    u32 T;                       // seconds since the game started
    u8 Fps, Thermal, LowPower, Hz;
    float EmuMs, RenderMs, R3DMs, MaxMs, DrawMs, GapMs;
    u16 Loading, Uploads, Hd2, Hd3, Draws, Passes, BufUps;
    float TexKB, BufKB;
};
std::vector<PerfSample> PerfLog;  // ring
std::mutex PerfLogLock;
size_t PerfLogNext = 0;
u64 PerfGameStart = 0;
std::atomic<int> HostThermal{0}, HostLowPower{0}, HostHz{0};
std::atomic<float> HostDrawMs{0};

// software renderer fallback: the RAM framebuffers go into this texture
GLuint SoftTex = 0;

std::string SavesDir() { return GameDir + "/Saves"; }

void OnNDSSave(const u8* data, u32 len, u32 off, u32 wlen)
{
    std::lock_guard<std::mutex> lk(SaveLock);
    if (SaveData.size() != len) SaveData.assign(data, data + len);
    else if (off + wlen <= len) memcpy(&SaveData[off], &data[off], wlen);
    else SaveData.assign(data, data + len);
    SaveDirty = true;
    SaveStamp = Platform::GetMSCount();
}

void FlushSave(bool force)
{
    std::vector<u8> copy;
    {
        std::lock_guard<std::mutex> lk(SaveLock);
        if (!SaveDirty || RomBase.empty()) return;
        if (!force && Platform::GetMSCount() - SaveStamp < 1500) return;
        copy = SaveData; SaveDirty = false;
    }
    wc09p::MakeDir(SavesDir());
    std::string path = SavesDir() + "/" + RomBase + ".sav", tmp = path + ".tmp";
    if (FILE* f = wc09p::Fopen(tmp, "wb"))
    {
        bool ok = fwrite(copy.data(), 1, copy.size(), f) == copy.size();
        ok = fclose(f) == 0 && ok;
        if (ok) wc09p::Rename(tmp, path);   // replaces the old file in one step
    }
}

bool ReadFile(const std::string& path, std::vector<u8>& out)
{
    return wc09p::ReadWhole(path, out) && !out.empty();
}

// screen drawing
GLuint Prog = 0, Vao = 0, Vbo = 0;
bool LinearFilter = true;
bool OptHdCards = true, OptHdText = true, OptSmooth2D = true;
int OptHdMode = 2;

// the master switch off = the original game: no HD layers, and no 2D smoothing for it either
void ApplyHdOptions()
{
    // modes: 0 = the game as on the DS, 1 = + HD card art, 2 = everything in HD but looking like the
    // original, 3 = + the redesign (new duel screen, menus, card text font).
    // Smooth 2D is a general picture setting, the same in every mode.
    int mode = WC09::IsActive() ? OptHdMode : 3;
    WC09::SetHDCardsEnabled(mode >= 1 && OptHdCards);
    WC09::SetHDPackEnabled(mode >= 2);
    WC09::SetHDTextEnabled(mode >= 2 && OptHdText);
    WC09::SetRedesignEnabled(mode >= 3);
    WC09::SetSmooth2D(OptSmooth2D);
}
GLint LocLayer = -1, LocSwap = -1;
#ifdef MELONDS_GLES
#define WC09_GLSL "#version 300 es\n"
#else
#define WC09_GLSL "#version 330 core\n"   // desktop OpenGL (macOS 4.1 core, Windows)
#endif
const char* kVS = WC09_GLSL R"(
in vec2 vPos; in vec2 vUV; out vec2 fUV;
void main() { gl_Position = vec4(vPos, 0.0, 1.0); fUV = vUV; })";
const char* kFS = WC09_GLSL R"(
precision highp float; precision highp sampler2DArray;
uniform sampler2DArray Tex; uniform float uLayer; uniform int uSwap; in vec2 fUV; out vec4 oColor;
void main() { vec3 c = texture(Tex, vec3(fUV, uLayer)).rgb; oColor = vec4(uSwap != 0 ? c.bgr : c, 1.0); })";

GLuint CompileOne(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); Platform::Log(Platform::LogLevel::Error, "screen shader: %s\n", log); }
    return s;
}

void InitScreenProgram()
{
    if (Prog) return;
    GLuint vs = CompileOne(GL_VERTEX_SHADER, kVS), fs = CompileOne(GL_FRAGMENT_SHADER, kFS);
    Prog = glCreateProgram();
    glAttachShader(Prog, vs); glAttachShader(Prog, fs);
    glBindAttribLocation(Prog, 0, "vPos"); glBindAttribLocation(Prog, 1, "vUV");
    glLinkProgram(Prog);
    glDeleteShader(vs); glDeleteShader(fs);
    glUseProgram(Prog);
    glUniform1i(glGetUniformLocation(Prog, "Tex"), 0);
    LocLayer = glGetUniformLocation(Prog, "uLayer");
    LocSwap = glGetUniformLocation(Prog, "uSwap");
    glGenVertexArrays(1, &Vao); glGenBuffers(1, &Vbo);
    glBindVertexArray(Vao);
    glBindBuffer(GL_ARRAY_BUFFER, Vbo);
    glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void ApplyRenderSettings()
{
    if (!Nds) return;
    SetPanelScale(Scale);
    RendererSettings s { .ScaleFactor = Scale, .Threaded = false, .HiresCoordinates = true, .BetterPolygons = false };
    Nds->GetRenderer().SetRenderSettings(s);
}
}

using namespace wc09m;

extern "C" {

int wc09_init(const char* gameDir, const char* dataDir, const char* fontDir)
{
    GameDir = gameDir ? gameDir : ""; DataDir = dataDir ? dataDir : ""; FontDir = fontDir ? fontDir : "";
    while (!GameDir.empty() && GameDir.back() == '/') GameDir.pop_back();
    wc09p::MakeDir(DataDir);
    return 0;
}

const char* wc09_find_rom(void)
{
    FoundRom.clear();
    for (const std::string& n : wc09p::ListDir(GameDir))
        if (n.size() > 4 && (n.compare(n.size() - 4, 4, ".nds") == 0 || n.compare(n.size() - 4, 4, ".NDS") == 0))
        { FoundRom = GameDir + "/" + n; break; }
    return FoundRom.c_str();
}

int wc09_load_rom(const char* path, int scale, int jit)
{
    std::vector<u8> rom;
    if (!path || !ReadFile(path, rom)) return 1;
    WC09::ReleaseROM();
    RomPath = path;
    { std::lock_guard<std::mutex> plk(PerfLogLock); PerfLog.clear(); PerfLogNext = 0; }
    PerfGameStart = 0; PerfLastCall = 0;
    std::string file = RomPath.substr(RomPath.rfind('/') + 1);
    RomBase = file.substr(0, file.rfind('.'));
    Scale = scale > 0 ? scale : 4;

    // in-game save: Saves/<rom>.sav (one left next to the game is moved in)
    wc09p::MakeDir(SavesDir());
    std::string sav = SavesDir() + "/" + RomBase + ".sav";
    std::string old = GameDir + "/" + RomBase + ".sav";
    if (!wc09p::Exists(sav) && wc09p::Exists(old)) wc09p::Rename(old, sav);
    std::vector<u8> savdata;
    ReadFile(sav, savdata);
    {
        std::lock_guard<std::mutex> lk(SaveLock);
        SaveData = savdata; SaveDirty = false;
    }

    NDSArgs args;
    if (!jit) args.JIT = std::nullopt;
    args.BitDepth = AudioBitDepth::_16Bit;
    args.Interpolation = AudioInterpolation::Cubic;
    args.OutputSampleRate = 48000.0;
    {
        auto& fw = args.Firmware;
        auto& ud = fw.GetEffectiveUserData();
        const char16_t name[] = u"Duelist";
        memset(ud.Nickname, 0, sizeof(ud.Nickname));
        for (int i = 0; i < 7; i++) ud.Nickname[i] = name[i];
        ud.NameLength = 7;
        ud.Settings = (ud.Settings & ~Firmware::Language::Reserved) | Firmware::Language::English;
        fw.UpdateChecksums();
    }

    {
        std::lock_guard<std::mutex> lk(AudioLock);
        Nds.reset();
        Nds = std::make_unique<NDS>(std::move(args), nullptr);
    }
    NDS::Current = Nds.get();
    { std::lock_guard<std::mutex> lk(LogLock); LogTail.clear(); }
    if (getenv("WC09_FORCE_SOFT")) Nds->SetRenderer(nullptr);   // testing the fallback
    else Nds->SetRenderer(std::make_unique<GLRenderer>(*Nds, false));
    ApplyRenderSettings();
    {
        void* t = nullptr; void* b = nullptr;
        bool soft = Nds->GPU.GetFramebuffers(&t, &b);
        GLint units = 0, ublock = 0, fvec = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &ublock);
#ifdef GL_MAX_FRAGMENT_UNIFORM_VECTORS
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fvec);
#endif
        std::lock_guard<std::mutex> lk(LogLock);
        Report = std::string(soft ? "renderer: SOFTWARE (OpenGL failed)\n" : "renderer: OpenGL\n")
            + "GL: " + (const char*)glGetString(GL_VERSION) + " / " + (const char*)glGetString(GL_RENDERER) + "\n"
            + "units " + std::to_string(units) + ", ublock " + std::to_string(ublock) + ", fvec " + std::to_string(fvec) + "\n"
            + LogTail;
        Platform::Log(Platform::LogLevel::Info, "%s\n", Report.c_str());
    }

    NDSCart::NDSCartArgs cartargs;
    if (!savdata.empty())
    {
        cartargs.SRAM = std::make_unique<u8[]>(savdata.size());
        memcpy(cartargs.SRAM.get(), savdata.data(), savdata.size());
        cartargs.SRAMLength = (u32)savdata.size();
    }
    std::unique_ptr<u8[]> romdata = std::make_unique<u8[]>(rom.size());
    memcpy(romdata.get(), rom.data(), rom.size());
    auto cart = NDSCart::ParseROM(std::move(romdata), (u32)rom.size(), nullptr, std::move(cartargs));
    if (!cart) return 2;
    Nds->SetNDSCart(std::move(cart));
    Nds->Reset();
    Nds->SetupDirectBoot(file);

    // the WC2009 HD work: on for this game when its HD pack sits next to the ROM (a folder with a
    // pack.json for the game: cards/, hd/, redesign/, data/cardtable.bin)
    const char* gc = Nds->NDSCartSlot.GetCart()->GetHeader().GameCode;
    std::string romDir = RomPath.substr(0, RomPath.rfind('/'));
    PackDir = FindPack(romDir, gc);
    if (PackDir.empty()) PackDir = FindPack(GameDir, gc);
    bool wc09 = gc[0] == 'C' && gc[1] == 'Y' && gc[2] == '8' && !PackDir.empty() &&
                WC09::LoadCardTable(PackDir + "/data/cardtable.bin");
    if (!wc09) PackDir.clear();
    WC09::SetActive(wc09);
    ApplyHdOptions();
    SetupHDProvider(PackDir, FontDir);

    Nds->Start();
    InitScreenProgram();
    return 0;
}

void wc09_set_scale(int scale)
{
    if (scale < 1 || scale > 16) return;
    Scale = scale;
    ApplyRenderSettings();
}
int wc09_get_scale(void) { return Scale; }

void wc09_run_frame(void)
{
    if (!Nds) return;
#ifdef MELONDS_GLES_30
    melonDS::GLES30::ForgetColorMask();
#endif
    NDS::Current = Nds.get();
    auto& r = Nds->GPU.GetRenderer();
    if (r.NeedsShaderCompile())
    {
        int cur = 0, count = 0;
        do { r.ShaderCompileStep(cur, count); } while (cur < count);
        return;
    }
    u64 t0 = Platform::GetUSCount();
    if (PerfLastCall) PerfMaxGapUs = std::max(PerfMaxGapUs, t0 - PerfLastCall);
    PerfLastCall = t0;
    if (!PerfGameStart) PerfGameStart = Platform::GetMSCount();
    {
        std::lock_guard<std::mutex> lk(AudioLock);
        Nds->RunFrame();
        PushAudio();
    }
    FlushSave(false);
    // performance numbers, averaged over about a second
    PerfFrames++;
    u64 took = Platform::GetUSCount() - t0;
    PerfUs += took;
    PerfMaxUs = std::max(PerfMaxUs, took);
    u64 now = Platform::GetMSCount();
    if (now - PerfStart >= 1000)
    {
        PerfFps = (int)(PerfFrames * 1000.0 / (now - PerfStart) + 0.5);
        auto& st = melonDS::GLES::St();
        double n = std::max<u64>(1, PerfFrames), secs = (now - PerfStart) / 1000.0;
        snprintf(PerfText, sizeof(PerfText), "%.0f fps  ·  %.1f ms/frame (max %.0f)  ·  %.0f draws  ·  %.0f+%.0f KB up  ·  %.1f reads  ·  %d loading",
                 PerfFrames / secs, PerfUs / n / 1000.0, PerfMaxUs / 1000.0, st.draws / n, st.bufBytes / n / 1024.0, st.texBytes / n / 1024.0, st.readbacks / n, WC09::PendingHDLoads());
        if (getenv("WC09_GLSTATS")) fprintf(stderr, "perf: %s (buf uploads %.0f, tex uploads %.0f)\n", PerfText, st.bufUploads / n, st.texUploads / n);
        {
            u64 rus = WC09Perf::RenderUs, r3 = WC09Perf::Render3DUs, up = WC09Perf::HdUploads;
            PerfSample s{};
            s.T = (u32)((now - PerfGameStart) / 1000);
            s.Fps = (u8)std::min(255, PerfFps);
            s.RenderMs = (float)((rus - PerfRender0) / n / 1000.0);
            s.R3DMs = (float)((r3 - PerfRender3D0) / n / 1000.0);
            s.EmuMs = (float)std::max(0.0, PerfUs / n / 1000.0 - s.RenderMs);
            s.MaxMs = PerfMaxUs / 1000.f;
            s.GapMs = PerfMaxGapUs / 1000.f;
            s.DrawMs = HostDrawMs; s.Hz = (u8)std::min(255, (int)HostHz); s.Thermal = (u8)HostThermal; s.LowPower = (u8)HostLowPower;
            s.Loading = (u16)std::min(65535, WC09::PendingHDLoads());
            s.Uploads = (u16)std::min<u64>(65535, up - PerfUploads0);
            s.Hd2 = (u16)WC09Perf::Hd2DCount; s.Hd3 = (u16)WC09Perf::Hd3DCount;
            s.Draws = (u16)std::min(65535.0, st.draws / n);
            s.Passes = (u16)std::min(65535.0, st.passBreaks / n);
            s.BufUps = (u16)std::min(65535.0, st.bufUploads / n);
            s.TexKB = (float)(st.texBytes / n / 1024.0); s.BufKB = (float)(st.bufBytes / n / 1024.0);
            PerfRender0 = rus; PerfRender3D0 = r3; PerfUploads0 = up;
            std::lock_guard<std::mutex> plk(PerfLogLock);
            if (PerfLog.size() < 900) PerfLog.push_back(s);
            else { PerfLog[PerfLogNext] = s; PerfLogNext = (PerfLogNext + 1) % PerfLog.size(); }
        }
        PerfMaxGapUs = 0;
        st = melonDS::GLES::Stats{};
        PerfFrames = 0; PerfUs = 0; PerfMaxUs = 0; PerfStart = now;
    }
}

void wc09_draw_screen(int screen, int x, int y, int w, int h, int fbWidth, int fbHeight)
{
    if (!Nds || !Prog) return;
    void* top = nullptr; void* bottom = nullptr;
    bool soft = Nds->GPU.GetFramebuffers(&top, &bottom);
    if (!top) return;
    GLuint tex;
    if (soft)
    {
        // the OpenGL renderer couldn't start: show the software renderer's picture
        if (!SoftTex)
        {
            glGenTextures(1, &SoftTex);
            glBindTexture(GL_TEXTURE_2D_ARRAY, SoftTex);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 256, 192, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, SoftTex);
        if (screen == 0)
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 256, 192, 1, GL_RGBA, GL_UNSIGNED_BYTE, top);
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, 256, 192, 1, GL_RGBA, GL_UNSIGNED_BYTE, bottom);
        }
        tex = SoftTex;
    }
    else tex = *(GLuint*)top;
    float x0 = 2.f * x / fbWidth - 1, x1 = 2.f * (x + w) / fbWidth - 1;
    float y0 = 1 - 2.f * y / fbHeight, y1 = 1 - 2.f * (y + h) / fbHeight;
    float v[16] = {x0, y0, 0, 0,  x1, y0, 1, 0,  x0, y1, 0, 1,  x1, y1, 1, 1};
    glViewport(0, 0, fbWidth, fbHeight);   // into the framebuffer the caller has bound
    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(Prog);
    glUniform1f(LocLayer, (float)(screen ? 1 : 0));
    glUniform1i(LocSwap, soft ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, LinearFilter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, LinearFilter ? GL_LINEAR : GL_NEAREST);
    glBindVertexArray(Vao);
    glBindBuffer(GL_ARRAY_BUFFER, Vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void wc09_gl_reset(void)
{
    // the old context is gone with every GL object in it: start over (the old renderer is
    // destroyed before the new one creates anything, so it can't delete the new objects)
    Prog = 0; Vao = 0; Vbo = 0; SoftTex = 0;
#ifdef MELONDS_GLES_30
    melonDS::GLES30::S() = melonDS::GLES30::State();
#endif
    if (!Nds) return;
    Nds->SetRenderer(std::make_unique<GLRenderer>(*Nds, false));
    ApplyRenderSettings();
    InitScreenProgram();
}

void wc09_set_keys(unsigned int pressed)
{
    if (!Nds) return;
    // core mask: bit set = released, 12 bits
    Nds->SetKeyMask(~pressed & 0xFFF);
}

void wc09_touch(int down, int x, int y)
{
    if (!Nds) return;
    if (down) Nds->TouchScreen((u16)std::min(255, std::max(0, x)), (u16)std::min(191, std::max(0, y)));
    else Nds->ReleaseScreen();
}

int wc09_audio_read(short* out, int frames)
{
    std::lock_guard<std::mutex> lk(RingLock);
    size_t cap = Ring.size() / 2;
    int n = (int)std::min<size_t>(RingCount, frames > 0 ? frames : 0);
    for (int i = 0; i < n; i++)
    {
        size_t r = (RingRead + i) % cap;
        out[i * 2] = Ring[r * 2]; out[i * 2 + 1] = Ring[r * 2 + 1];
    }
    RingRead = (RingRead + n) % cap; RingCount -= n;
    return n;
}

int wc09_audio_queued(void) { std::lock_guard<std::mutex> lk(RingLock); return (int)RingCount; }

static std::string StatePath(int slot) { return SavesDir() + "/" + RomBase + ".ml" + std::to_string(slot); }

int wc09_save_state(int slot)
{
    if (!Nds) return 1;
    Savestate st;
    if (st.Error || !Nds->DoSavestate(&st) || st.Error) return 2;
    wc09p::MakeDir(SavesDir());
    FILE* f = wc09p::Fopen(StatePath(slot), "wb");
    if (!f) return 3;
    bool ok = fwrite(st.Buffer(), 1, st.Length(), f) == st.Length();
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : 4;
}

int wc09_load_state(int slot) { return wc09_load_state_file(StatePath(slot).c_str()); }

int wc09_load_state_file(const char* path)
{
    if (!Nds) return 1;
    std::vector<u8> buf;
    if (!ReadFile(path, buf)) return 2;
    Savestate st(buf.data(), (u32)buf.size(), false);
    std::lock_guard<std::mutex> lk(AudioLock);
    if (!Nds->DoSavestate(&st) || st.Error) return 3;
    return 0;
}

int wc09_state_exists(int slot) { return wc09p::Exists(StatePath(slot)) ? 1 : 0; }
int wc09_delete_state(int slot) { return wc09p::Remove(StatePath(slot)) ? 0 : 1; }

void wc09_flush_save(void) { FlushSave(true); }

void wc09_shutdown(void)
{
    // call with the game's GL context current: the renderer and screen program are deleted
    FlushSave(true);
    WC09::ReleaseROM();
    {
        std::lock_guard<std::mutex> lk(AudioLock);
        Nds.reset();
        NDS::Current = nullptr;
    }
    if (Prog) { glDeleteProgram(Prog); glDeleteVertexArrays(1, &Vao); glDeleteBuffers(1, &Vbo); }
    if (SoftTex) glDeleteTextures(1, &SoftTex);
    Prog = 0; Vao = 0; Vbo = 0; SoftTex = 0;
#ifdef MELONDS_GLES_30
    melonDS::GLES30::S() = melonDS::GLES30::State();
#endif
    { std::lock_guard<std::mutex> lk(RingLock); RingCount = 0; }
    RomBase.clear();
}

void wc09_set_option(int option, int value)
{
    switch (option)
    {
    case WC09_OPT_HD_CARDS: OptHdCards = value != 0; break;
    case WC09_OPT_HD_TEXT: OptHdText = value != 0; break;
    case WC09_OPT_SMOOTH_2D: OptSmooth2D = value != 0; break;
    case WC09_OPT_HD_ALL: OptHdMode = value ? 2 : 0; break;
    case WC09_OPT_HD_MODE: OptHdMode = std::clamp(value, 0, 3); break;
    case WC09_OPT_LINEAR_FILTER: LinearFilter = value != 0; break;
    }
    ApplyHdOptions();
}

const char* wc09_perf_text(void) { return PerfText; }
int wc09_fps(void) { return PerfFps; }

const char* wc09_renderer_report(int* software)
{
    void* t = nullptr; void* b = nullptr;
    if (software) *software = Nds && Nds->GPU.GetFramebuffers(&t, &b) ? 1 : 0;
    return Report.c_str();
}

int wc09_rom_code(const char* path, char* code)
{
    code[0] = 0;
    FILE* f = wc09p::Fopen(path, "rb");
    if (!f) return 1;
    bool ok = fseek(f, 0x0C, SEEK_SET) == 0 && fread(code, 1, 4, f) == 4;
    fclose(f);
    code[ok ? 4 : 0] = 0;
    return ok ? 0 : 2;
}

int wc09_is_hd_game(void) { return Nds && WC09::IsActive() ? 1 : 0; }

int wc09_rom_info(const char* path, char* title, int titleCap, unsigned char* rgba)
{
    if (title && titleCap > 0) title[0] = 0;
    if (rgba) memset(rgba, 0, 32 * 32 * 4);
    FILE* f = wc09p::Fopen(path, "rb");
    if (!f) return 1;
    u8 hdr[0x200];
    u8 banner[0x840];
    bool ok = fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    u32 off = ok ? (hdr[0x68] | hdr[0x69] << 8 | hdr[0x6A] << 16 | (u32)hdr[0x6B] << 24) : 0;
    ok = ok && off != 0 && fseek(f, off, SEEK_SET) == 0 && fread(banner, 1, sizeof(banner), f) == sizeof(banner);
    fclose(f);
    if (!ok) return 2;
    if (rgba)
    {
        // 4bpp, 4x4 tiles of 8x8; palette entry 0 is transparent
        u32 pal[16];
        for (int i = 0; i < 16; i++)
        {
            u16 c = banner[0x220 + i * 2] | banner[0x221 + i * 2] << 8;
            u32 r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
            pal[i] = r | g << 8 | b << 16 | (i ? 0xFF000000u : 0);
        }
        for (int t = 0; t < 16; t++)
            for (int py = 0; py < 8; py++)
                for (int px = 0; px < 8; px++)
                {
                    u8 v = banner[0x20 + t * 32 + py * 4 + px / 2];
                    int idx = (px & 1) ? (v >> 4) : (v & 15);
                    int x = (t % 4) * 8 + px, y = (t / 4) * 8 + py;
                    u32 c = pal[idx];
                    memcpy(&rgba[(y * 32 + x) * 4], &c, 4);
                }
    }
    if (title && titleCap > 1)
    {
        // English title (falls back to Japanese), UTF-16 -> UTF-8
        const u8* t16 = &banner[0x240 + 0x100];
        if (!(t16[0] | t16[1])) t16 = &banner[0x240];
        int n = 0;
        for (int i = 0; i < 128 && n < titleCap - 4; i++)
        {
            u32 c = t16[i * 2] | t16[i * 2 + 1] << 8;
            if (!c) break;
            if (c < 0x80) title[n++] = (char)c;
            else if (c < 0x800) { title[n++] = (char)(0xC0 | c >> 6); title[n++] = (char)(0x80 | (c & 63)); }
            else { title[n++] = (char)(0xE0 | c >> 12); title[n++] = (char)(0x80 | ((c >> 6) & 63)); title[n++] = (char)(0x80 | (c & 63)); }
        }
        title[n] = 0;
    }
    return 0;
}

}

extern "C" {
void wc09_set_host_info(int thermal, int lowPower, float drawMs, int displayHz)
{
    HostThermal = thermal; HostLowPower = lowPower; HostDrawMs = drawMs; HostHz = displayHz;
}

// one line per second around the slow moments, quiet stretches folded into one line each
const char* wc09_debug_report(void)
{
    static std::string out;
    out.clear();
    char b[512];
    void* fbT = nullptr; void* fbB = nullptr;
    std::vector<PerfSample> v;
    {
        std::lock_guard<std::mutex> plk(PerfLogLock);
        for (size_t i = 0; i < PerfLog.size(); i++) v.push_back(PerfLog[(PerfLogNext + i) % PerfLog.size()]);
    }
    snprintf(b, sizeof(b), "Dual HD debug report\ngame file %s  scale %dx  renderer %s\n", RomBase.c_str(), Scale, Nds && !Nds->GPU.GetFramebuffers(&fbT, &fbB) ? "OpenGL" : "software");
    out += b;
    int slow = 0;
    for (auto& s : v) if (s.Fps < 57) slow++;
    snprintf(b, sizeof(b), "%zu seconds logged, %d below 57 fps\n", v.size(), slow);
    out += b;
    out += "columns: time fps | emu ms, render ms (3d), max ms, gap ms, draw ms, display hz | loading, hd uploads, hd2d/hd3d cached, draws, tex KB, buf KB, buf updates, pass breaks | thermal, low power\n";
    auto line = [&](const PerfSample& s) {
        snprintf(b, sizeof(b), "%02u:%02u %2u | %.1f %.1f(%.1f) %.0f %.0f %.1f %u | %u %u %u/%u %u %.0f %.0f %u %u | %u %u\n",
                 s.T / 60, s.T % 60, s.Fps, s.EmuMs, s.RenderMs, s.R3DMs, s.MaxMs, s.GapMs, s.DrawMs, s.Hz,
                 s.Loading, s.Uploads, s.Hd2, s.Hd3, s.Draws, s.TexKB, s.BufKB, s.BufUps, s.Passes, s.Thermal, s.LowPower);
        out += b;
    };
    std::vector<bool> show(v.size(), false);
    for (size_t i = 0; i < v.size(); i++)
        if (v[i].Fps < 57)
            for (size_t j = (i >= 5 ? i - 5 : 0); j < std::min(v.size(), i + 4); j++) show[j] = true;
    for (size_t i = 0; i < v.size();)
    {
        if (show[i]) { line(v[i]); i++; continue; }
        size_t j = i; double emu = 0, ren = 0; int th = 0;
        while (j < v.size() && !show[j]) { emu += v[j].EmuMs; ren += v[j].RenderMs; th = std::max<int>(th, v[j].Thermal); j++; }
        snprintf(b, sizeof(b), "%02u:%02u-%02u:%02u fine: emu %.1f ms, render %.1f ms, thermal %d\n",
                 v[i].T / 60, v[i].T % 60, v[j - 1].T / 60, v[j - 1].T % 60, emu / (j - i), ren / (j - i), th);
        out += b;
        i = j;
    }
    return out.c_str();
}
}
