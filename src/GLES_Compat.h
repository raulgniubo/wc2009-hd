// WC09 fork: OpenGL ES 3.2 (Android) / ES 3.0 (iOS) support for the OpenGL renderer.
// Desktop-only GL calls the renderer uses are mapped to their ES equivalents here.
#ifndef GLES_COMPAT_H
#define GLES_COMPAT_H

#if defined(__APPLE__)
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#ifndef MELONDS_GLES_30
#define MELONDS_GLES_30 1
#endif
#elif defined(MELONDS_GLES_30)
// ES 3.0 path on other systems (used to test the iOS code path with Mesa)
#include <GLES3/gl3.h>
#else
#include <GLES3/gl32.h>
#endif
#include <vector>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9   // never used on ES 3.0 (no compute renderer)
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

// per-frame GL statistics (Settings > Show performance, and WC09_GLSTATS in the test harness)
namespace melonDS { namespace GLES {
struct Stats { unsigned draws, bufUploads, bufBytes, texUploads, texBytes, readbacks; };
inline Stats& St() { static Stats s{}; return s; }
}}

namespace melonDS { namespace GLES {
// RGB5_A1 data in the desktop "1_5_5_5_REV" layout (DS colour: bits 0-4 red, 15 alpha) -> RGBA8
inline const void* Convert5551(const void* src, size_t count)
{
    static thread_local std::vector<uint32_t> buf;
    if (!src) return nullptr;
    if (buf.size() < count) buf.resize(count);
    const uint16_t* s = (const uint16_t*)src;
    for (size_t i = 0; i < count; i++)
    {
        uint32_t c = s[i];
        uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
        buf[i] = r | (g << 8) | (b << 16) | ((c & 0x8000) ? 0xFF000000u : 0);
    }
    return buf.data();
}
inline GLenum Internal(GLenum internal) { return internal == GL_RGB5_A1 ? GL_RGBA8 : internal; }
}}

// texture uploads: the 15-bit DS formats become RGBA8
inline void glesTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void* d)
{
    if (d) { melonDS::GLES::St().texUploads++; melonDS::GLES::St().texBytes += w * h * 4; }
    if (ty == GL_UNSIGNED_SHORT_1_5_5_5_REV) { d = melonDS::GLES::Convert5551(d, (size_t)w * h); ty = GL_UNSIGNED_BYTE; ifmt = GL_RGBA8; f = GL_RGBA; }
    glTexImage2D(t, l, ifmt, w, h, b, f, ty, d);
}
inline void glesTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void* d)
{
    melonDS::GLES::St().texUploads++; melonDS::GLES::St().texBytes += w * h * 4;
    if (ty == GL_UNSIGNED_SHORT_1_5_5_5_REV) { d = melonDS::GLES::Convert5551(d, (size_t)w * h); ty = GL_UNSIGNED_BYTE; f = GL_RGBA; }
    glTexSubImage2D(t, l, x, y, w, h, f, ty, d);
}
inline void glesTexImage3D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLsizei dp, GLint b, GLenum f, GLenum ty, const void* d)
{
    if (ty == GL_UNSIGNED_SHORT_1_5_5_5_REV) { d = melonDS::GLES::Convert5551(d, (size_t)w * h * dp); ty = GL_UNSIGNED_BYTE; ifmt = GL_RGBA8; f = GL_RGBA; }
    glTexImage3D(t, l, ifmt, w, h, dp, b, f, ty, d);
}
inline void glesTexSubImage3D(GLenum t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei dp, GLenum f, GLenum ty, const void* d)
{
    melonDS::GLES::St().texUploads++; melonDS::GLES::St().texBytes += w * h * dp * 4;
    if (ty == GL_UNSIGNED_SHORT_1_5_5_5_REV) { d = melonDS::GLES::Convert5551(d, (size_t)w * h * dp); ty = GL_UNSIGNED_BYTE; f = GL_RGBA; }
    glTexSubImage3D(t, l, x, y, z, w, h, dp, f, ty, d);
}
#define glTexImage2D glesTexImage2D
#define glTexSubImage2D glesTexSubImage2D
#define glTexImage3D glesTexImage3D
#define glTexSubImage3D glesTexSubImage3D

// uniform buffer binding points: the renderer uses 0, 20-25 and 30-31; GLES only guarantees 24
inline GLuint UBOBinding(GLuint b) { return b >= 30 ? b - 23 : (b >= 20 ? b - 19 : b); }
inline void glesUniformBlockBinding(GLuint p, GLuint idx, GLuint b) { glUniformBlockBinding(p, idx, UBOBinding(b)); }
inline void glesBindBufferBase(GLenum t, GLuint i, GLuint buf);
#define glUniformBlockBinding glesUniformBlockBinding
// Buffer updates never touch storage the GPU may still be reading: every update re-specifies the
// whole buffer from a CPU copy ("orphaning"), so the driver hands out fresh memory instead of
// making the CPU wait. GL-on-Metal (iPhone) otherwise stalls on each of the ~100 updates a frame.
namespace melonDS { namespace GLES {
struct BufInfo { std::vector<uint8_t> shadow; GLenum usage = GL_STREAM_DRAW; bool valid = false; };
inline std::unordered_map<GLuint, BufInfo>& Bufs() { static std::unordered_map<GLuint, BufInfo> m; return m; }
inline GLuint& Bound(GLenum t)
{
    static GLuint arr = 0, elem = 0, uni = 0, other = 0;
    return t == GL_ARRAY_BUFFER ? arr : t == GL_ELEMENT_ARRAY_BUFFER ? elem : t == GL_UNIFORM_BUFFER ? uni : other;
}
}}
inline void glesBindBuffer(GLenum t, GLuint b) { melonDS::GLES::Bound(t) = b; glBindBuffer(t, b); }
inline void glesBufferData(GLenum t, GLsizeiptr n, const void* d, GLenum u)
{
    GLuint b = melonDS::GLES::Bound(t);
    if (b)
    {
        auto& bi = melonDS::GLES::Bufs()[b];
        bi.usage = u;
        bi.valid = true;
        if (d) bi.shadow.assign((const uint8_t*)d, (const uint8_t*)d + n);
        else bi.shadow.assign((size_t)n, 0);
    }
    glBufferData(t, n, d, u);
}
inline void glesBufferSubData(GLenum t, GLintptr o, GLsizeiptr n, const void* d)
{
    melonDS::GLES::St().bufUploads++; melonDS::GLES::St().bufBytes += (unsigned)n;
    auto& m = melonDS::GLES::Bufs();
    auto it = m.find(melonDS::GLES::Bound(t));
    if (it != m.end() && it->second.valid && o >= 0 && (size_t)(o + n) <= it->second.shadow.size())
    {
        memcpy(&it->second.shadow[o], d, (size_t)n);
        glBufferData(t, (GLsizeiptr)it->second.shadow.size(), it->second.shadow.data(), it->second.usage);
        return;
    }
    glBufferSubData(t, o, n, d);
}
inline void glesDeleteBuffers(GLsizei n, const GLuint* ids)
{
    for (GLsizei i = 0; i < n; i++) melonDS::GLES::Bufs().erase(ids[i]);
    glDeleteBuffers(n, ids);
}
#define glBindBuffer glesBindBuffer
#define glBufferData glesBufferData
#define glBufferSubData glesBufferSubData
#define glDeleteBuffers glesDeleteBuffers
inline void glesBindBufferBase(GLenum t, GLuint i, GLuint buf)
{
    melonDS::GLES::Bound(t) = buf;   // also binds the generic target
    glBindBufferBase(t, t == GL_UNIFORM_BUFFER ? UBOBinding(i) : i, buf);
}
inline void glesReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, void* d)
{
    melonDS::GLES::St().readbacks++;
    if (ty != GL_UNSIGNED_SHORT_1_5_5_5_REV) { glReadPixels(x, y, w, h, f, ty, d); return; }
    static thread_local std::vector<uint8_t> tmp;
    tmp.resize((size_t)w * h * 4);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
    uint16_t* o = (uint16_t*)d;
    for (size_t i = 0; i < (size_t)w * h; i++)
    {
        const uint8_t* p = &tmp[i * 4];
        o[i] = (uint16_t)((p[0] >> 3) | ((p[1] >> 3) << 5) | ((p[2] >> 3) << 10) | (p[3] ? 0x8000 : 0));
    }
}
#define glReadPixels glesReadPixels
#define glBindBufferBase glesBindBufferBase

#ifdef MELONDS_GLES_30
// ES 3.0 (iOS) has no per-attachment colour masks. Attachment 0 is always either fully on or fully
// off in the renderer: when it's off, attachment 1 is drawn alone (draw buffer 0 = NONE); when it's
// on, the shaders mask attachment 1 themselves with framebuffer fetch (uWC09Mask1, see
// OpenGLSupport.cpp).
namespace melonDS { namespace GLES30 {
struct ProgState { GLint Loc = -2; GLfloat Last[4] = {-1, -1, -1, -1}; };
struct State
{
    GLboolean Mask[2][4] = {{1, 1, 1, 1}, {1, 1, 1, 1}};
    GLuint Prog = 0, DrawFB = 0;
    std::unordered_map<GLuint, std::vector<GLenum>> FBBufs;
    std::unordered_map<GLuint, ProgState> Progs;
    GLfloat ClearCol[4] = {0, 0, 0, 0};
};
inline State& S() { static State s; return s; }
inline const std::vector<GLenum>* Bufs()
{
    auto it = S().FBBufs.find(S().DrawFB);
    return (S().DrawFB != 0 && it != S().FBBufs.end()) ? &it->second : nullptr;
}
inline bool TwoTargets() { const auto* b = Bufs(); return b && b->size() >= 2 && (*b)[1] != GL_NONE; }
inline bool Any(const GLboolean* m) { return m[0] || m[1] || m[2] || m[3]; }
// sets up the masks for a draw; returns true if the draw buffers have to be restored after
inline bool PrepareDraw()
{
    State& s = S();
    GLfloat m1[4];
    bool restore = false;
    if (!Any(s.Mask[0]) && TwoTargets())
    {
        GLenum b[2] = {GL_NONE, (*Bufs())[1]};
        glDrawBuffers(2, b);
        restore = true;
        glColorMask(s.Mask[1][0], s.Mask[1][1], s.Mask[1][2], s.Mask[1][3]);
        m1[0] = m1[1] = m1[2] = m1[3] = 1;
    }
    else
    {
        glColorMask(s.Mask[0][0], s.Mask[0][1], s.Mask[0][2], s.Mask[0][3]);
        for (int i = 0; i < 4; i++) m1[i] = s.Mask[1][i] ? 1.f : 0.f;
    }
    if (s.Prog)
    {
        ProgState& p = s.Progs[s.Prog];
        if (p.Loc == -2) p.Loc = glGetUniformLocation(s.Prog, "uWC09Mask1");
        if (p.Loc >= 0 && memcmp(p.Last, m1, sizeof(m1)))
        {
            glUniform4fv(p.Loc, 1, m1);
            memcpy(p.Last, m1, sizeof(m1));
        }
    }
    return restore;
}
inline void FinishDraw(bool restore)
{
    if (!restore) return;
    const auto* b = Bufs();
    if (b) glDrawBuffers((GLsizei)b->size(), b->data());
}
}}

inline void glColorMaski(GLuint buf, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    if (buf > 1) return;
    GLboolean* m = melonDS::GLES30::S().Mask[buf];
    m[0] = r; m[1] = g; m[2] = b; m[3] = a;
}
inline void gles30ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    glColorMaski(0, r, g, b, a); glColorMaski(1, r, g, b, a);
    glColorMask(r, g, b, a);
}
inline void gles30UseProgram(GLuint p) { melonDS::GLES30::S().Prog = p; glUseProgram(p); }
inline void gles30DeleteProgram(GLuint p)
{
    auto& s = melonDS::GLES30::S();
    s.Progs.erase(p);
    if (s.Prog == p) s.Prog = 0;
    glDeleteProgram(p);
}
inline void gles30BindFramebuffer(GLenum t, GLuint fb)
{
    if (t == GL_FRAMEBUFFER || t == GL_DRAW_FRAMEBUFFER) melonDS::GLES30::S().DrawFB = fb;
    glBindFramebuffer(t, fb);
}
inline void gles30DrawBuffers(GLsizei n, const GLenum* b)
{
    auto& s = melonDS::GLES30::S();
    if (s.DrawFB) s.FBBufs[s.DrawFB].assign(b, b + n);
    glDrawBuffers(n, b);
}
inline void gles30ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLfloat* c = melonDS::GLES30::S().ClearCol;
    c[0] = r; c[1] = g; c[2] = b; c[3] = a;
    glClearColor(r, g, b, a);
}
inline void gles30Clear(GLbitfield m)
{
    using namespace melonDS::GLES30;
    State& s = S();
    if ((m & GL_COLOR_BUFFER_BIT) && TwoTargets() && memcmp(s.Mask[0], s.Mask[1], 4))
    {
        for (int i = 0; i < 2; i++)
        {
            glColorMask(s.Mask[i][0], s.Mask[i][1], s.Mask[i][2], s.Mask[i][3]);
            glClearBufferfv(GL_COLOR, i, s.ClearCol);
        }
        m &= ~GL_COLOR_BUFFER_BIT;
        glColorMask(s.Mask[0][0], s.Mask[0][1], s.Mask[0][2], s.Mask[0][3]);
        if (m) glClear(m);
        return;
    }
    glColorMask(s.Mask[0][0], s.Mask[0][1], s.Mask[0][2], s.Mask[0][3]);
    glClear(m);
}
inline void gles30DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    melonDS::GLES::St().draws++;
    bool r = melonDS::GLES30::PrepareDraw();
    glDrawArrays(mode, first, count);
    melonDS::GLES30::FinishDraw(r);
}
inline void gles30DrawElements(GLenum mode, GLsizei count, GLenum type, const void* idx)
{
    melonDS::GLES::St().draws++;
    bool r = melonDS::GLES30::PrepareDraw();
    glDrawElements(mode, count, type, idx);
    melonDS::GLES30::FinishDraw(r);
}
#define glColorMask gles30ColorMask
#define glUseProgram gles30UseProgram
#define glDeleteProgram gles30DeleteProgram
#define glBindFramebuffer gles30BindFramebuffer
#define glDrawBuffers gles30DrawBuffers
#define glClearColor gles30ClearColor
#define glClear gles30Clear
#define glDrawArrays gles30DrawArrays
#define glDrawElements gles30DrawElements
#else
inline void glesDrawArrays(GLenum m, GLint f, GLsizei c) { melonDS::GLES::St().draws++; glDrawArrays(m, f, c); }
inline void glesDrawElements(GLenum m, GLsizei c, GLenum t, const void* i) { melonDS::GLES::St().draws++; glDrawElements(m, c, t, i); }
#define glDrawArrays glesDrawArrays
#define glDrawElements glesDrawElements
#endif

inline void glClearDepth(double d) { glClearDepthf((float)d); }
inline void glDepthRange(double n, double f) { glDepthRangef((float)n, (float)f); }
inline void glDrawBuffer(GLenum b) { glDrawBuffers(1, &b); }
inline void* glMapBuffer(GLenum target, GLenum)
{
    { auto it = melonDS::GLES::Bufs().find(melonDS::GLES::Bound(target)); if (it != melonDS::GLES::Bufs().end()) it->second.valid = false; }
    GLint size = 0; glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    return glMapBufferRange(target, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
}
// output locations are set in the shader source instead (see OpenGLSupport.cpp)
inline void glBindFragDataLocation(GLuint, GLuint, const char*) {}

#ifdef MELONDS_GLES_30
// iOS (ES 3.0): no border clamp (the compositor clamps in the shader), no layered attach (all the
// attached textures are 2D)
inline void glFramebufferTexture(GLenum t, GLenum a, GLuint tex, GLint l) { glFramebufferTexture2D(t, a, GL_TEXTURE_2D, tex, l); }
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER GL_CLAMP_TO_EDGE
#endif
#endif

#endif
