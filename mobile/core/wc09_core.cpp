// WC2009 HD core for the mobile apps: melonDS + the WC09 HD work, OpenGL ES renderer.
#include "wc09_core.h"

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

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
#include "WC09Cards.h"
#include "WC09Render.h"

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
    mkdir(SavesDir().c_str(), 0755);
    std::string path = SavesDir() + "/" + RomBase + ".sav", tmp = path + ".tmp";
    if (FILE* f = fopen(tmp.c_str(), "wb"))
    {
        bool ok = fwrite(copy.data(), 1, copy.size(), f) == copy.size();
        ok = fclose(f) == 0 && ok;
        if (ok) rename(tmp.c_str(), path.c_str());
    }
}

bool ReadFile(const std::string& path, std::vector<u8>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n > 0 && fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

// screen drawing
GLuint Prog = 0, Vao = 0, Vbo = 0;
bool LinearFilter = true;
bool OptHdAll = true, OptHdCards = true, OptHdText = true, OptSmooth2D = true;

// the master switch off = the original game: no HD layers, and no 2D smoothing for it either
void ApplyHdOptions()
{
    bool all = OptHdAll || !WC09::IsActive();
    WC09::SetHDCardsEnabled(all && OptHdCards);
    WC09::SetHDTextEnabled(all && OptHdText);
    WC09::SetSmooth2D(all && OptSmooth2D);
}
GLint LocLayer = -1;
const char* kVS = R"(#version 300 es
in vec2 vPos; in vec2 vUV; out vec2 fUV;
void main() { gl_Position = vec4(vPos, 0.0, 1.0); fUV = vUV; })";
const char* kFS = R"(#version 300 es
precision highp float; precision highp sampler2DArray;
uniform sampler2DArray Tex; uniform float uLayer; in vec2 fUV; out vec4 oColor;
void main() { oColor = vec4(texture(Tex, vec3(fUV, uLayer)).rgb, 1.0); })";

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
    mkdir(DataDir.c_str(), 0755);
    return 0;
}

const char* wc09_find_rom(void)
{
    FoundRom.clear();
    if (DIR* d = opendir(GameDir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            std::string n = e->d_name;
            if (n.size() > 4 && (n.compare(n.size() - 4, 4, ".nds") == 0 || n.compare(n.size() - 4, 4, ".NDS") == 0) && n[0] != '.')
            { FoundRom = GameDir + "/" + n; break; }
        }
        closedir(d);
    }
    return FoundRom.c_str();
}

int wc09_load_rom(const char* path, int scale, int jit)
{
    std::vector<u8> rom;
    if (!path || !ReadFile(path, rom)) return 1;
    RomPath = path;
    std::string file = RomPath.substr(RomPath.rfind('/') + 1);
    RomBase = file.substr(0, file.rfind('.'));
    Scale = scale > 0 ? scale : 4;

    // in-game save: Saves/<rom>.sav (one left next to the game is moved in)
    mkdir(SavesDir().c_str(), 0755);
    std::string sav = SavesDir() + "/" + RomBase + ".sav";
    std::string old = GameDir + "/" + RomBase + ".sav";
    struct stat st;
    if (stat(sav.c_str(), &st) != 0 && stat(old.c_str(), &st) == 0) rename(old.c_str(), sav.c_str());
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
    Nds->SetRenderer(std::make_unique<GLRenderer>(*Nds, false));
    ApplyRenderSettings();

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

    // the WC2009 HD work: on for this game
    const char* gc = Nds->NDSCartSlot.GetCart()->GetHeader().GameCode;
    WC09::SetActive(gc[0] == 'C' && gc[1] == 'Y' && gc[2] == '8');
    ApplyHdOptions();
    SetupHDProvider(GameDir + "/HDCards", FontDir);

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
    NDS::Current = Nds.get();
    auto& r = Nds->GPU.GetRenderer();
    if (r.NeedsShaderCompile())
    {
        int cur = 0, count = 0;
        do { r.ShaderCompileStep(cur, count); } while (cur < count);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(AudioLock);
        Nds->RunFrame();
        PushAudio();
    }
    FlushSave(false);
}

void wc09_draw_screen(int screen, int x, int y, int w, int h, int fbWidth, int fbHeight)
{
    if (!Nds || !Prog) return;
    void* top = nullptr; void* bottom = nullptr;
    if (Nds->GPU.GetFramebuffers(&top, &bottom) || !top) return;
    GLuint tex = *(GLuint*)top;
    float x0 = 2.f * x / fbWidth - 1, x1 = 2.f * (x + w) / fbWidth - 1;
    float y0 = 1 - 2.f * y / fbHeight, y1 = 1 - 2.f * (y + h) / fbHeight;
    float v[16] = {x0, y0, 0, 0,  x1, y0, 1, 0,  x0, y1, 0, 1,  x1, y1, 1, 1};
    glViewport(0, 0, fbWidth, fbHeight);   // into the framebuffer the caller has bound
    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(Prog);
    glUniform1f(LocLayer, (float)(screen ? 1 : 0));
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
    Prog = 0; Vao = 0; Vbo = 0;
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
    mkdir(SavesDir().c_str(), 0755);
    FILE* f = fopen(StatePath(slot).c_str(), "wb");
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

int wc09_state_exists(int slot) { struct stat st; return stat(StatePath(slot).c_str(), &st) == 0; }
int wc09_delete_state(int slot) { return remove(StatePath(slot).c_str()) == 0 ? 0 : 1; }

void wc09_flush_save(void) { FlushSave(true); }

void wc09_shutdown(void)
{
    // call with the game's GL context current: the renderer and screen program are deleted
    FlushSave(true);
    {
        std::lock_guard<std::mutex> lk(AudioLock);
        Nds.reset();
        NDS::Current = nullptr;
    }
    if (Prog) { glDeleteProgram(Prog); glDeleteVertexArrays(1, &Vao); glDeleteBuffers(1, &Vbo); }
    Prog = 0; Vao = 0; Vbo = 0;
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
    case WC09_OPT_HD_ALL: OptHdAll = value != 0; break;
    case WC09_OPT_LINEAR_FILTER: LinearFilter = value != 0; break;
    }
    ApplyHdOptions();
}

int wc09_is_hd_game(void) { return Nds && WC09::IsActive() ? 1 : 0; }

int wc09_rom_info(const char* path, char* title, int titleCap, unsigned char* rgba)
{
    if (title && titleCap > 0) title[0] = 0;
    if (rgba) memset(rgba, 0, 32 * 32 * 4);
    FILE* f = fopen(path, "rb");
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
