// The few raw OpenGL calls the game view makes. They live in their own file so glad never meets
// Qt's OpenGL headers (on macOS Qt includes Apple's gl.h/gl3.h/glext.h, which clash with glad).
#pragma once
namespace gamegl
{
void Load(void* (*getProc)(const char* name));
void Clear(unsigned fbo, int w, int h);   // bind the window's framebuffer and clear it to black
void Bind(unsigned fbo);
// put back the GL state Qt's painter expects (the emulator leaves its own textures, samplers,
// buffers and pixel-store settings bound, which garbles the overlay text otherwise)
void ResetForPainter(unsigned fbo);
}
