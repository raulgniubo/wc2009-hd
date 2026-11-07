// C API of the WC2009 HD core used by the iOS and Android apps.
// Threading: everything except wc09_audio_read and the input calls must run on the thread that
// owns the OpenGL ES context.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// DS buttons (bit set = pressed)
enum
{
    WC09_KEY_A = 1 << 0, WC09_KEY_B = 1 << 1, WC09_KEY_SELECT = 1 << 2, WC09_KEY_START = 1 << 3,
    WC09_KEY_RIGHT = 1 << 4, WC09_KEY_LEFT = 1 << 5, WC09_KEY_UP = 1 << 6, WC09_KEY_DOWN = 1 << 7,
    WC09_KEY_R = 1 << 8, WC09_KEY_L = 1 << 9, WC09_KEY_X = 1 << 10, WC09_KEY_Y = 1 << 11,
};

// gameDir: folder holding the .nds and HDCards; dataDir: writable folder for settings/caches;
// fontDir: folder holding Inter-Medium.ttf and Inter-Bold.ttf. Returns 0 on success.
int wc09_init(const char* gameDir, const char* dataDir, const char* fontDir);
// first .nds in gameDir, or "" (pointer valid until the next call)
const char* wc09_find_rom(void);
// loads and boots the ROM (saves in gameDir/Saves); needs the GL context current. 0 on success.
int wc09_load_rom(const char* path, int scale, int jit);
void wc09_set_scale(int scale);           // 4, 6 or 8 (any 1..16 works)
int wc09_get_scale(void);

// runs one DS frame (about 1/60 s of emulation); call once per display refresh
void wc09_run_frame(void);
// draws screen 0 (top) or 1 (bottom) at x,y,w,h (pixels, top-left origin) into the framebuffer
// the caller binds first (the emulator leaves its own framebuffers bound after a frame)
void wc09_draw_screen(int screen, int x, int y, int w, int h, int fbWidth, int fbHeight);

// the GL context was lost and a new one is current: recreate the renderer (the game keeps running)
void wc09_gl_reset(void);

void wc09_set_keys(unsigned int pressed);  // WC09_KEY_* mask
void wc09_touch(int down, int x, int y);   // bottom screen, DS pixels (0..255, 0..191)

// interleaved stereo s16 at 48000 Hz; returns frames written (thread-safe, never waits for the
// emulator: call it from the audio callback)
int wc09_audio_read(short* out, int frames);
int wc09_audio_queued(void);   // frames waiting

int wc09_save_state(int slot);   // 0 on success
int wc09_load_state(int slot);
int wc09_state_exists(int slot);
int wc09_delete_state(int slot);   // 0 on success
int wc09_load_state_file(const char* path);   // any .mlN file (e.g. one from the desktop version)
void wc09_flush_save(void);      // write pending in-game save data now (call when backgrounded)
void wc09_shutdown(void);

#ifdef __cplusplus
}
#endif
