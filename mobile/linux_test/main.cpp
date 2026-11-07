// Headless OpenGL ES 3 test of the mobile core (Mesa EGL, no window):
// wc09test GAMEDIR FONTDIR STATE FRAMES OUT.png [SCALE] [KEYS...]
// Loads the ROM in GAMEDIR, the save state, runs FRAMES frames and writes both screens to OUT.png.
// KEYS: "f<frame>:<keymask hex>" to press buttons at a frame, "t<frame>:x,y" to tap.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <chrono>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../core/third_party/stb_image_write.h"
#include "wc09_core.h"

int main(int argc, char** argv)
{
    if (argc < 6) { fprintf(stderr, "usage: wc09test GAMEDIR FONTDIR STATE FRAMES OUT.png [SCALE] [KEYS]\n"); return 1; }
    const char* gameDir = argv[1]; const char* fontDir = argv[2]; const char* state = argv[3];
    int frames = atoi(argv[4]); const char* out = argv[5];
    int scale = argc > 6 ? atoi(argv[6]) : 4;
    std::map<int, unsigned> keys; std::map<int, std::pair<int, int>> taps; std::multimap<int, std::pair<int, int>> opts;
    for (int i = 7; i < argc; i++)
    {
        if (argv[i][0] == 'f') { int f; unsigned k; if (sscanf(argv[i] + 1, "%d:%x", &f, &k) == 2) keys[f] = k; }
        if (argv[i][0] == 'o') { int f, o, v; if (sscanf(argv[i] + 1, "%d:%d,%d", &f, &o, &v) == 3) opts.insert({f, {o, v}}); }
        if (argv[i][0] == 't') { int f, x, y; if (sscanf(argv[i] + 1, "%d:%d,%d", &f, &x, &y) == 3) taps[f] = {x, y}; }
    }

    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, nullptr, nullptr)) { fprintf(stderr, "egl init failed\n"); return 2; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig cfg = EGL_NO_CONFIG_KHR;   // surfaceless: no config needed
    int W = 256 * scale, H = 384 * scale;
    int esMinor = getenv("WC09_ES30") ? 0 : 2;
    EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, esMinor, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fprintf(stderr, "no context\n"); return 4; }
    fprintf(stderr, "GL: %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    wc09_init(gameDir, "/tmp/wc09test_data", fontDir);
    const char* rom = wc09_find_rom();
    if (!rom[0]) { fprintf(stderr, "no rom in %s\n", gameDir); return 5; }
    if (wc09_load_rom(rom, scale, 1)) { fprintf(stderr, "rom load failed\n"); return 6; }
    if (state[0] && strcmp(state, "-") && wc09_load_state_file(state)) { fprintf(stderr, "state load failed\n"); return 7; }

    auto t0 = std::chrono::steady_clock::now();
    long audioFrames = 0; int peak = 0;
    for (int f = 0; f < frames; f++)
    {
        if (f == frames / 2 && getenv("WC09_GLRESET"))
        {
            // simulate a lost context (Android can drop it when the app is backgrounded)
            EGLContext ctx2 = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx2);
            eglDestroyContext(dpy, ctx);
            ctx = ctx2;
            wc09_gl_reset();
            fprintf(stderr, "context replaced at frame %d\n", f);
        }
        if (f == frames / 2 && getenv("WC09_RELOAD"))
        {
            // back to the library and into the game again
            wc09_shutdown();
            if (wc09_load_rom(rom, scale, 1) || (state[0] && strcmp(state, "-") && wc09_load_state_file(state))) { fprintf(stderr, "reload failed\n"); return 8; }
            fprintf(stderr, "reloaded at frame %d\n", f);
        }
        for (auto it = opts.lower_bound(f); it != opts.upper_bound(f); ++it) wc09_set_option(it->second.first, it->second.second);
        if (keys.count(f)) wc09_set_keys(keys[f]);
        if (taps.count(f)) wc09_touch(1, taps[f].first, taps[f].second);
        if (taps.count(f - 3)) wc09_touch(0, 0, 0);
        wc09_run_frame();
        short audio[2048];
        int got = wc09_audio_read(audio, 1024);
        audioFrames += got;
        for (int i = 0; i < got * 2; i++) peak = std::max(peak, abs((int)audio[i]));
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "audio: %ld frames (%.1f per video frame), peak %d\n", audioFrames, (double)audioFrames / std::max(1, frames), peak);
    fprintf(stderr, "%d frames in %.0f ms (%.1f ms/frame)\n", frames, ms, ms / std::max(1, frames));
    if (getenv("WC09_REPORT")) fprintf(stderr, "%s", wc09_debug_report());

    GLuint fbo, rb;
    glGenFramebuffers(1, &fbo); glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    wc09_draw_screen(0, 0, 0, W, H / 2, W, H);
    wc09_draw_screen(1, 0, H / 2, W, H / 2, W, H);
    std::vector<unsigned char> px((size_t)W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    GLenum err = glGetError();
    if (err) fprintf(stderr, "GL error 0x%x\n", err);
    // the draw call maps y=0 to the top of the target (as on screen); an FBO read starts at the
    // bottom row, so flip
    std::vector<unsigned char> flip(px.size());
    for (int y = 0; y < H; y++) memcpy(&flip[(size_t)y * W * 4], &px[(size_t)(H - 1 - y) * W * 4], (size_t)W * 4);
    stbi_write_png(out, W, H, 4, flip.data(), W * 4);
    wc09_shutdown();
    return 0;
}
