// Game controllers and audio through SDL2.
#pragma once
#include <QString>

namespace pads
{
struct State
{
    unsigned Held = 0;     // controller buttons held (bit per app::PadButton)
    unsigned Dirs = 0;     // WC09_KEY_UP/DOWN/LEFT/RIGHT from the D-pad and left stick
    bool Nintendo = false; // a Switch-style pad (A on the right)
    bool Connected = false;
};
bool Init();               // SDL video-less init: controllers + audio
void Quit();
// handles connects/disconnects and reads the first controller; call often (every frame)
State Poll();
QString Name();            // first controller's name, "" if none
int Count();
}

namespace audio
{
bool Start();              // 48 kHz stereo from wc09_audio_read
void Stop();
void SetVolume(int percent);   // 0..100
void SetMuted(bool m);
}
