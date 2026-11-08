#include "glad/glad.h"
#include "GameGL.h"
#include <algorithm>

namespace gamegl
{
void Load(void* (*getProc)(const char* name)) { gladLoadGLLoader((GLADloadproc)getProc); }

void Clear(unsigned fbo, int w, int h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Bind(unsigned fbo) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }

static GLsync Fence = nullptr;

void WaitLastFrame()
{
    if (!Fence) return;
    glClientWaitSync(Fence, GL_SYNC_FLUSH_COMMANDS_BIT, 50000000);   // 50 ms at most
    glDeleteSync(Fence);
    Fence = nullptr;
}

void MarkFrame()
{
    if (Fence) glDeleteSync(Fence);
    Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void ForgetFrames()
{
    if (Fence) glDeleteSync(Fence);
    Fence = nullptr;
}

void ResetForPainter(unsigned fbo)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    GLint units = 16;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &units);
    for (int i = std::min(units, 32) - 1; i >= 0; i--)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
        glBindSampler(i, 0);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
}
}
