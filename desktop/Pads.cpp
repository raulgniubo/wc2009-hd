#include "Pads.h"
#include "App.h"
#include "wc09_core.h"
#define SDL_MAIN_HANDLED   // Qt owns main()
#include <SDL.h>
#include <vector>
#include <atomic>
#include <cstring>

namespace pads
{
static std::vector<SDL_GameController*> Open;
static bool Inited = false;

bool Init()
{
    if (Inited) return true;
    // read buttons by position everywhere (Nintendo pads would otherwise swap A/B and X/Y)
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    Inited = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0;
    return Inited;
}

void Quit()
{
    for (SDL_GameController* c : Open) SDL_GameControllerClose(c);
    Open.clear();
    if (Inited) SDL_Quit();
    Inited = false;
}

static void Refresh()
{
    for (SDL_GameController* c : Open) SDL_GameControllerClose(c);
    Open.clear();
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i))
            if (SDL_GameController* c = SDL_GameControllerOpen(i)) Open.push_back(c);
}

State Poll()
{
    State s;
    if (!Inited) return s;
    static bool first = true;
    if (first) { first = false; Refresh(); }
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) Refresh();
    SDL_GameControllerUpdate();
    for (SDL_GameController* c : Open)
    {
        if (!SDL_GameControllerGetAttached(c)) continue;
        s.Connected = true;
        static const SDL_GameControllerButton map[app::PB_COUNT] = {
            SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
            SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
            SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
            SDL_CONTROLLER_BUTTON_GUIDE};
        for (int b = 0; b < app::PB_COUNT; b++)
            if (map[b] != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(c, map[b])) s.Held |= 1u << b;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) s.Held |= 1u << app::PB_L2;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) s.Held |= 1u << app::PB_R2;
        int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP) || y < -16000) s.Dirs |= WC09_KEY_UP;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || y > 16000) s.Dirs |= WC09_KEY_DOWN;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || x < -16000) s.Dirs |= WC09_KEY_LEFT;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || x > 16000) s.Dirs |= WC09_KEY_RIGHT;
        SDL_GameControllerType t = SDL_GameControllerGetType(c);
        s.Nintendo = t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO
#if SDL_VERSION_ATLEAST(2, 24, 0)
                     || t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT || t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT
                     || t == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR
#endif
            ;
        break;   // the first controller plays
    }
    return s;
}

QString Name()
{
    for (SDL_GameController* c : Open)
        if (SDL_GameControllerGetAttached(c)) { const char* n = SDL_GameControllerName(c); return n ? QString::fromUtf8(n) : "Controller"; }
    return {};
}
int Count() { int n = 0; for (SDL_GameController* c : Open) if (SDL_GameControllerGetAttached(c)) n++; return n; }
}

namespace audio
{
static SDL_AudioDeviceID Dev = 0;
static std::atomic<int> Volume{100};
static std::atomic<bool> Muted{false};
static short Last[2] = {0, 0};

static void Callback(void*, Uint8* stream, int len)
{
    short* out = (short*)stream;
    int frames = len / 4;
    int got = wc09_audio_read(out, frames);
    int vol = Muted ? 0 : Volume.load();
    if (got > 0) { Last[0] = out[got * 2 - 2]; Last[1] = out[got * 2 - 1]; }
    // not enough yet: fade the last sample out instead of clicking to zero
    for (int i = got; i < frames; i++)
    {
        Last[0] = (short)(Last[0] * 15 / 16); Last[1] = (short)(Last[1] * 15 / 16);
        out[i * 2] = Last[0]; out[i * 2 + 1] = Last[1];
    }
    if (vol < 100)
        for (int i = 0; i < frames * 2; i++) out[i] = (short)(out[i] * vol / 100);
}

bool Start()
{
    if (Dev) return true;
    SDL_AudioSpec want{}, have{};
    want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512; want.callback = Callback;
    Dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!Dev) return false;
    SDL_PauseAudioDevice(Dev, 0);
    return true;
}
void Stop() { if (Dev) { SDL_CloseAudioDevice(Dev); Dev = 0; } }
void SetVolume(int p) { Volume = p < 0 ? 0 : p > 100 ? 100 : p; }
void SetMuted(bool m) { Muted = m; }
}
