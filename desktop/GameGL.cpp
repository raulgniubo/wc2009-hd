#include "glad/glad.h"
#include "GameGL.h"

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
}
