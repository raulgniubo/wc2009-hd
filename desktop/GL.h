// glad, included before any Qt OpenGL header
#pragma once
#include "glad/glad.h"
#ifdef __APPLE__
// Qt pulls in <OpenGL/gl3.h> on macOS, which would clash with glad's declarations
#define __gl3_h_
#define __gl3ext_h_
#endif
