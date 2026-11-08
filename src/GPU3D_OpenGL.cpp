/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "WC09Perf.h"
#include "GPU_OpenGL.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "NDS.h"
#include "GPU.h"
#include "WC09Battle.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "WC09Cards.h"
#include <vector>
#include <string>

namespace melonDS
{

#include "OpenGL_shaders/3DClearVS.h"
#include "OpenGL_shaders/3DClearFS.h"
#include "OpenGL_shaders/3DClearBitmapVS.h"
#include "OpenGL_shaders/3DClearBitmapFS.h"
#include "OpenGL_shaders/3DRenderVS.h"
#include "OpenGL_shaders/3DRenderFS.h"
#include "OpenGL_shaders/3DFinalPassVS.h"
#include "OpenGL_shaders/3DFinalPassEdgeFS.h"
#include "OpenGL_shaders/3DFinalPassFogFS.h"

bool GLRenderer3D::BuildRenderShader(bool wbuffer)
{
    std::string wbufdef = "#define WBuffer\n";

    char shadername[32];
    snprintf(shadername, sizeof(shadername), "RenderShader%c", wbuffer?'W':'Z');

    std::string vsbuf = k3DRenderVS;
    if (wbuffer)
    {
        auto pos = vsbuf.find('\n') + 1;
        vsbuf = vsbuf.substr(0, pos) + wbufdef + vsbuf.substr(pos);
    }

    std::string fsbuf = k3DRenderFS;
    if (wbuffer)
    {
        auto pos = fsbuf.find('\n') + 1;
        fsbuf = fsbuf.substr(0, pos) + wbufdef + fsbuf.substr(pos);
    }

    GLuint prog;
    bool ret = OpenGL::CompileVertexFragmentProgram(prog,
        vsbuf, fsbuf,
        shadername,
        {{"vPosition", 0}, {"vColor", 1}, {"vTexcoord", 2}, {"vPolygonAttr", 3}},
        {{"oColor", 0}, {"oAttr", 1}});

    if (!ret) return false;

    GLint uni_id = glGetUniformBlockIndex(prog, "uConfig");
    glUniformBlockBinding(prog, uni_id, 0);

    glUseProgram(prog);

    uni_id = glGetUniformLocation(prog, "CurTexture");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(prog, "Capture128Texture");
    glUniform1i(uni_id, 1);
    uni_id = glGetUniformLocation(prog, "Capture256Texture");
    glUniform1i(uni_id, 2);
    uni_id = glGetUniformLocation(prog, "HDTexture");
    glUniform1i(uni_id, 3);

    RenderShader[(int)wbuffer] = prog;

    return true;
}

void GLRenderer3D::UseRenderShader(bool wbuffer)
{
    int flags = (int)wbuffer;
    if (CurShaderID == flags) return;
    glUseProgram(RenderShader[flags]);
    CurShaderID = flags;

    RenderModeULoc = glGetUniformLocation(RenderShader[flags], "uRenderMode");
    HDModeULoc = glGetUniformLocation(RenderShader[flags], "uHDMode");
}

// Textures the game redraws while they're on screen (scrolling light, flicker, ...) come in many
// versions; an HD pack only covers some of them, so the picture would jump between the HD and the
// original one. A texture slot whose contents keep changing gets the original version only.
bool GLRenderer3D::AnimatedTexture(u32 texparam, u64 hash, u32 w, u32 h, u32 textype)
{
    if (textype == 4 || w < 32 || h < 32) return false;          // UI pieces: handled by their own rules
    u32 key = (texparam & 0xFFFF) | (w << 16) | (h << 24);
    TexSlot& t = TexSlots[key];
    if (t.Hash != hash)
    {
        if (t.Hash && HD3DFrame - t.LastChange < 30) t.Changes++;
        else t.Changes = 0;
        t.Hash = hash; t.LastChange = HD3DFrame;
        if (t.Changes >= 6) t.AnimatedUntil = HD3DFrame + 1800;
    }
    if (TexSlots.size() > 4096) TexSlots.clear();
    static const bool dbg = getenv("WC09_ANIMDBG") != nullptr;
    if (dbg && t.AnimatedUntil > HD3DFrame && t.Changes == 6) { printf("WC09 animated texture slot %08x\n", key); fflush(stdout); }
    return t.AnimatedUntil > HD3DFrame;
}

GLuint GLRenderer3D::GetHDNamed(const char* name)
{
    auto it = HDNamed.find(name);
    if (it != HDNamed.end()) return it->second;
    std::vector<u8> rgba; int w = 0, h = 0;
    GLuint t = 0;
    if (WC09::LoadHDImage(name, rgba, w, h) && w > 0 && h > 0) t = UploadHD3D(rgba.data(), w, h);
    HDNamed[name] = t;
    return t;
}

GLuint GLRenderer3D::UploadHD3D(const u8* rgba, int w, int h)
{
    WC09Perf::HdUploads++;
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    // anisotropic filtering: roads and floors seen at a slant stay sharp without shimmering
    static GLfloat maxAniso = -1.f;
    if (maxAniso < 0.f)
    {
        maxAniso = 0.f;
        GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; i++)
        {
            const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
            if (ext && (!strcmp(ext, "GL_EXT_texture_filter_anisotropic") || !strcmp(ext, "GL_ARB_texture_filter_anisotropic")))
            { glGetFloatv(0x84FF /* MAX_TEXTURE_MAX_ANISOTROPY */, &maxAniso); break; }
        }
    }
    if (maxAniso > 1.f) glTexParameterf(GL_TEXTURE_2D, 0x84FE /* TEXTURE_MAX_ANISOTROPY */, std::min(8.f, maxAniso));
    glActiveTexture(GL_TEXTURE0);
    return tex;
}


// WC09: a small texture is text when it shows dark outline + bright fill with only a few
// colours on a transparent background. Rebuilt 8x larger: the bright fill is cut from a cubic
// interpolation of brightness, the outline from a cubic interpolation of coverage.
static bool SmoothTextTexture(const u32* px, int w, int h, std::vector<u8>& out)
{
    int opaque = 0, bright = 0, dark = 0; u32 cols[24]; int nc = 0;
    for (int i = 0; i < w * h; i++)
    {
        u32 c = px[i];
        if (!(c >> 24)) continue;
        opaque++;
        int r = c & 0x3F, g = (c >> 8) & 0x3F, b = (c >> 16) & 0x3F;
        int l = (r * 3 + g * 6 + b) / 10;
        int mx = std::max({r, g, b}), mn = std::min({r, g, b});
        if (l > 50 && mx - mn < 10) bright++;      // white / light grey fill
        if (l < 14) dark++;
        u32 k = c & 0x3F3F3F; bool seen = false;
        for (int j = 0; j < nc && !seen; j++) seen = cols[j] == k;
        if (!seen) { if (nc == 16) return false; cols[nc++] = k; }
    }
    if (opaque < 12 || bright < 6 || dark < 6 || opaque > w * h * 3 / 5) return false;
    auto at = [&](int x, int y) { x = std::clamp(x, 0, w - 1); y = std::clamp(y, 0, h - 1); return px[y * w + x]; };
    auto cr = [](float t, float* wt) {
        float t2 = t * t, t3 = t2 * t;
        wt[0] = -0.5f * t3 + t2 - 0.5f * t; wt[1] = 1.5f * t3 - 2.5f * t2 + 1.f;
        wt[2] = -1.5f * t3 + 2.f * t2 + 0.5f * t; wt[3] = 0.5f * t3 - 0.5f * t2;
    };
    const int S = 8, W = w * S, H = h * S;
    out.assign((size_t)W * H * 4, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            float fx = (x + 0.5f) / S - 0.5f, fy = (y + 0.5f) / S - 0.5f;
            int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
            float wx[4], wy[4]; cr(fx - ix, wx); cr(fy - iy, wy);
            float A = 0, F = 0, best = -1; u32 hue = 0xFFFFFF;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++)
                {
                    u32 c = at(ix + i - 1, iy + j - 1);
                    float a = (c >> 24) ? 1.f : 0.f, wgt = wx[i] * wy[j];
                    float m = std::max({(int)(c & 0x3F), (int)((c >> 8) & 0x3F), (int)((c >> 16) & 0x3F)}) / 63.f * a;
                    A += wgt * a; F += wgt * m;
                    if (i >= 1 && i <= 2 && j >= 1 && j <= 2 && m > best) { best = m; hue = c; }
                }
            u8* o = &out[((size_t)y * W + x) * 4];
            if (A < 0.5f) continue;
            float t = std::clamp((F - 0.40f) / 0.22f, 0.f, 1.f); t = t * t * (3 - 2 * t);
            float hm = std::max(best, 0.001f);
            for (int k = 0; k < 3; k++)
            {
                float hc = ((hue >> (8 * k)) & 0x3F) / 63.f / hm * 0.97f;
                o[k] = (u8)std::clamp((0.04f * (1 - t) + std::min(hc, 1.f) * t) * 255.f + 0.5f, 0.f, 255.f);
            }
            o[3] = 255;
        }
    return true;
}

GLuint GLRenderer3D::GetHD3D(u64 hash, u64 alphaHash, u64 halfHash, u16 tone, const u32* px, int pw, int ph)
{
    auto it = HD3D.find(hash);
    if (it != HD3D.end()) { it->second.LastUse = HD3DFrame; return it->second.Valid ? it->second.Tex : 0; }
    HD3DEntry e {0, false, HD3DFrame};
    char name[40]; snprintf(name, sizeof(name), "tex3d/%016llx", (unsigned long long)hash);
    std::vector<u8> rgba; int w = 0, h = 0;
    // redesigned pictures first (they apply to every colour variant): tex3d/s_<structure hash>,
    // tex3d/h_<structure hash of the top half>; then the exact texture: tex3d/<content hash>
    std::vector<std::string> names;
    if (alphaHash)
    {
        // colour states first: s_<hash>_<hue><brightness>, s_<hash>_<hue>, then any colour
        char an[48];
        if (tone)
        {
            snprintf(an, sizeof(an), "tex3d/s_%016llx_%c%c", (unsigned long long)alphaHash, tone >> 8, tone & 0xFF); names.push_back(an);
            snprintf(an, sizeof(an), "tex3d/s_%016llx_%c", (unsigned long long)alphaHash, tone >> 8); names.push_back(an);
        }
        snprintf(an, sizeof(an), "tex3d/s_%016llx", (unsigned long long)alphaHash); names.push_back(an);
    }
    if (halfHash)
    {
        char an[40]; snprintf(an, sizeof(an), "tex3d/h_%016llx", (unsigned long long)halfHash); names.push_back(an);
    }
    names.push_back(name);
    // the pictures are decoded on the worker thread (big PNGs would stall the game for a moment):
    // the original texture shows until the HD one is ready
    int& stage = HD3DStage[hash];
    bool ok = false;
    while (stage < (int)names.size())
    {
        int r = WC09::LoadHDImageAsync(names[stage], rgba, w, h);
        if (r == 0) { HDPending = true; return 0; }   // still loading
        if (r > 0 && w > 0 && h > 0) { ok = true; break; }
        stage++;                           // not there: next name
    }
    HD3DStage.erase(hash);
    if (!ok && px && pw * ph <= 64 * 32 && SmoothTextTexture(px, pw, ph, rgba))
    {
        // small text textures (names under cards in lists...): redrawn with smooth edges
        w = pw * 8; h = ph * 8; ok = true;
    }
    if (ok)
    {
        e.Tex = UploadHD3D(rgba.data(), w, h);
        e.Valid = true;
    }
    HD3D[hash] = e;
    if (HD3D.size() > 384)   // forget textures not seen for half a minute (GPU memory on phones)
        for (auto it = HD3D.begin(); it != HD3D.end();)
            if (HD3DFrame - it->second.LastUse > 1800 && it->first != hash) { if (it->second.Valid) glDeleteTextures(1, &it->second.Tex); it = HD3D.erase(it); }
            else ++it;
    static const bool debug = getenv("WC09_DEBUG") != nullptr;
    if (debug) { printf("WC09 tex3d %s s_%016llx_%c%c %s\n", name, (unsigned long long)alphaHash, tone ? tone >> 8 : '-', tone ? tone & 0xFF : '-', e.Valid ? "HD" : "-"); fflush(stdout); }
    return e.Valid ? e.Tex : 0;
}

GLuint GLRenderer3D::GetHDCard(u64 hash, int card, u32 size, const u32* newPixels)
{
    // keep a copy of the decoded card texture: the HD version keeps its layout and its
    // transparency, only the card face area gets the HD image
    if (newPixels)
    {
        CardPx& c = CardPixels[hash];
        c.Px.assign(newPixels, newPixels + size * size);
        c.LastUse = HD3DFrame;
        if (CardPixels.size() > 512)
            for (auto it = CardPixels.begin(); it != CardPixels.end();)
                it = (HD3DFrame - it->second.LastUse > 600 && it->first != hash) ? CardPixels.erase(it) : std::next(it);
    }
    auto it = HD3D.find(hash);
    if (it != HD3D.end()) { it->second.LastUse = HD3DFrame; return it->second.Valid ? it->second.Tex : 0; }
    auto px = CardPixels.find(hash);
    if (px == CardPixels.end() || px->second.Px.size() != size * size) return 0;
    px->second.LastUse = HD3DFrame;

    HD3DEntry e {0, false, HD3DFrame};
    std::vector<u8> src; int sw = 0, sh = 0;
    const bool thumb = card & 0x10000;
    card &= 0xFFFF;
    int lr = WC09::LoadCardImageAsync(card, src, sw, sh);
    if (lr == 0) { HDPending = true; return 0; }   // loading on the worker thread: the original texture meanwhile
    if (lr > 0 && sw > 1 && sh > 1)
    {
        const u32* orig = px->second.Px.data();
        const int S = size == 32 ? 8 : 512 / (int)size, W = (int)size * S;
        std::vector<u8> out((size_t)W * W * 4);
        auto origAt = [&](int x, int y) { return orig[(y / S) * size + (x / S)]; };
        // the parts outside the face (frame bars of the hand-card textures...) keep the original
        // pixels, upscaled smoothly between opaque neighbours; transparency stays as it was
        auto chan = [&](int tx, int ty, int k) -> float {
            tx = std::clamp(tx, 0, (int)size - 1); ty = std::clamp(ty, 0, (int)size - 1);
            u32 c = orig[ty * size + tx];
            return (float)((c >> (8 * k)) & 0x3F) * 255.f / 63.f;
        };
        auto opaque = [&](int tx, int ty) {
            tx = std::clamp(tx, 0, (int)size - 1); ty = std::clamp(ty, 0, (int)size - 1);
            return ((orig[ty * size + tx] >> 24) & 0x1F) != 0;
        };
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
            {
                u32 c = origAt(x, y); u8* o = &out[((size_t)y * W + x) * 4];
                o[3] = ((c >> 24) & 0x1F) * 255 / 31;
                float fx = (x + 0.5f) / S - 0.5f, fy = (y + 0.5f) / S - 0.5f;
                int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                float tx = fx - x0, ty = fy - y0;
                bool all = opaque(x0, y0) && opaque(x0 + 1, y0) && opaque(x0, y0 + 1) && opaque(x0 + 1, y0 + 1);
                for (int k = 0; k < 3; k++)
                {
                    if (!all) { o[k] = (u8)(((c >> (8 * k)) & 0x3F) * 255 / 63); continue; }
                    float a = chan(x0, y0, k) * (1 - tx) + chan(x0 + 1, y0, k) * tx;
                    float b = chan(x0, y0 + 1, k) * (1 - tx) + chan(x0 + 1, y0 + 1, k) * tx;
                    o[k] = (u8)(a * (1 - ty) + b * ty + 0.5f);
                }
            }
        // face rectangle in texels. 128/64: the whole face at texels 20..108 (of 128) across, full
        // height. 32 (hand, field, small cards): the game draws the whole texture as a little card
        // (art plus coloured bars); it gets the whole HD card instead.
        float dx0, dy0, dx1, dy1, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (thumb) { dx0 = 6; dy0 = 3; dx1 = 26; dy1 = 29; }    // list thumbnails: the little card
        else if (size == 32) { dx0 = 0; dy0 = 0; dx1 = 32; dy1 = 32; }
        else { dx0 = size * 20.f / 128.f; dx1 = size * 108.f / 128.f; dy0 = 0; dy1 = (float)size; }
        int px0 = (int)(dx0 * S), px1 = (int)(dx1 * S), py0 = (int)(dy0 * S), py1 = (int)(dy1 * S);
        for (int y = py0; y < py1; y++)
        {
            float v = v0 + (y + 0.5f - py0) / (py1 - py0) * (v1 - v0);
            float fy = v * sh - 0.5f; int y0 = std::clamp((int)std::floor(fy), 0, sh - 1), y1 = std::min(y0 + 1, sh - 1);
            float ty = std::clamp(fy - y0, 0.f, 1.f);
            for (int x = px0; x < px1; x++)
            {
                float u = u0 + (x + 0.5f - px0) / (px1 - px0) * (u1 - u0);
                float fx = u * sw - 0.5f; int x0 = std::clamp((int)std::floor(fx), 0, sw - 1), x1 = std::min(x0 + 1, sw - 1);
                float tx = std::clamp(fx - x0, 0.f, 1.f);
                u8* o = &out[((size_t)y * W + x) * 4];
                for (int k = 0; k < 3; k++)
                {
                    float a = src[((size_t)y0 * sw + x0) * 4 + k] * (1 - tx) + src[((size_t)y0 * sw + x1) * 4 + k] * tx;
                    float b = src[((size_t)y1 * sw + x0) * 4 + k] * (1 - tx) + src[((size_t)y1 * sw + x1) * 4 + k] * tx;
                    o[k] = (u8)(a * (1 - ty) + b * ty + 0.5f);
                }
                // keep the original's transparency (rounded corners), opaque elsewhere in the face
                if ((size == 32 && !thumb) || ((origAt(x, y) >> 24) & 0x1F) != 0) o[3] = 255;
            }
        }
        e.Tex = UploadHD3D(out.data(), W, W);
        e.Valid = true;
    }
    HD3D[hash] = e;
    static const bool debug = getenv("WC09_DEBUG") != nullptr;
    if (debug) { printf("WC09 card3d %d (%u px) %s\n", card, size, e.Valid ? "HD" : "-"); fflush(stdout); }
    return e.Valid ? e.Tex : 0;
}

void SetupDefaultTexParams(GLuint tex)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

GLRenderer3D::GLRenderer3D(melonDS::GPU3D& gpu3D, GLRenderer& parent) noexcept :
    Renderer3D(gpu3D), Parent(parent), Texcache(gpu3D.GPU, TexcacheOpenGLLoader(false))
{
    ClearBitmap[0] = new u32[256*256];
    ClearBitmap[1] = new u32[256*256];

    ScaleFactor = 0;
    BetterPolygons = false;

    // GLRenderer3D::Init() will be used to actually initialize the renderer;
    // The various glDelete* functions silently ignore invalid IDs,
    // so we can just let the destructor clean up a half-initialized renderer.
}

bool GLRenderer3D::Init()
{
    GLint uni_id;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);

    glDepthRange(0, 1);
    glClearDepth(1.0);

    if (!OpenGL::CompileVertexFragmentProgram(ClearShaderPlain,
            k3DClearVS, k3DClearFS,
            "ClearShaderPlain",
            {{"vPosition", 0}},
            {{"oColor", 0}, {"oAttr", 1}}))
        return false;

    ClearUniformLoc[0] = glGetUniformLocation(ClearShaderPlain, "uColor");
    ClearUniformLoc[1] = glGetUniformLocation(ClearShaderPlain, "uDepth");
    ClearUniformLoc[2] = glGetUniformLocation(ClearShaderPlain, "uOpaquePolyID");
    ClearUniformLoc[3] = glGetUniformLocation(ClearShaderPlain, "uFogFlag");

    if (!OpenGL::CompileVertexFragmentProgram(ClearShaderBitmap,
              k3DClearBitmapVS, k3DClearBitmapFS,
              "ClearShaderBitmap",
              {{"vPosition", 0}},
              {{"oColor", 0}, {"oAttr", 1}}))
        return false;

    ClearBitmapULoc[0] = glGetUniformLocation(ClearShaderBitmap, "uClearBitmapOffset");
    ClearBitmapULoc[1] = glGetUniformLocation(ClearShaderBitmap, "uOpaquePolyID");

    glUseProgram(ClearShaderBitmap);
    uni_id = glGetUniformLocation(ClearShaderBitmap, "ClearBitmapColor");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(ClearShaderBitmap, "ClearBitmapDepth");
    glUniform1i(uni_id, 1);

    memset(RenderShader, 0, sizeof(RenderShader));

    if (!BuildRenderShader(false))
        return false;

    if (!BuildRenderShader(true))
        return false;

    if (!OpenGL::CompileVertexFragmentProgram(FinalPassEdgeShader,
            k3DFinalPassVS, k3DFinalPassEdgeFS,
            "FinalPassEdgeShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        return false;
    if (!OpenGL::CompileVertexFragmentProgram(FinalPassFogShader,
            k3DFinalPassVS, k3DFinalPassFogFS,
            "FinalPassFogShader",
            {{"vPosition", 0}},
            {{"oColor", 0}}))
        return false;

    uni_id = glGetUniformBlockIndex(FinalPassEdgeShader, "uConfig");
    glUniformBlockBinding(FinalPassEdgeShader, uni_id, 0);

    glUseProgram(FinalPassEdgeShader);
    uni_id = glGetUniformLocation(FinalPassEdgeShader, "DepthBuffer");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(FinalPassEdgeShader, "AttrBuffer");
    glUniform1i(uni_id, 1);

    uni_id = glGetUniformBlockIndex(FinalPassFogShader, "uConfig");
    glUniformBlockBinding(FinalPassFogShader, uni_id, 0);

    glUseProgram(FinalPassFogShader);
    uni_id = glGetUniformLocation(FinalPassFogShader, "DepthBuffer");
    glUniform1i(uni_id, 0);
    uni_id = glGetUniformLocation(FinalPassFogShader, "AttrBuffer");
    glUniform1i(uni_id, 1);


    memset(&ShaderConfig, 0, sizeof(ShaderConfig));

    glGenBuffers(1, &ShaderConfigUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, ShaderConfigUBO);
    static_assert((sizeof(ShaderConfig) & 15) == 0);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ShaderConfig), &ShaderConfig, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ShaderConfigUBO);


    float clearvtx[6*2] =
    {
        -1.0, -1.0,
        1.0, 1.0,
        -1.0, 1.0,

        -1.0, -1.0,
        1.0, -1.0,
        1.0, 1.0
    };

    glGenBuffers(1, &ClearVertexBufferID);
    glBindBuffer(GL_ARRAY_BUFFER, ClearVertexBufferID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(clearvtx), clearvtx, GL_STATIC_DRAW);

    glGenVertexArrays(1, &ClearVertexArrayID);
    glBindVertexArray(ClearVertexArrayID);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)(0));

    // init textures for the clear bitmap
    glGenTextures(2, ClearBitmapTex);

    glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, 256, 256, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 256, 256, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);


    glGenBuffers(1, &VertexBufferID);
    glBindBuffer(GL_ARRAY_BUFFER, VertexBufferID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexBuffer), nullptr, GL_DYNAMIC_DRAW);

    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);
    glEnableVertexAttribArray(0); // position
    glVertexAttribIPointer(0, 4, GL_UNSIGNED_SHORT, 7*4, (void*)(0));
    glEnableVertexAttribArray(1); // color
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, 7*4, (void*)(2*4));
    glEnableVertexAttribArray(2); // texcoords
    glVertexAttribIPointer(2, 2, GL_SHORT, 7*4, (void*)(3*4));
    glEnableVertexAttribArray(3); // attrib
    glVertexAttribIPointer(3, 3, GL_UNSIGNED_INT, 7*4, (void*)(4*4));

    glGenBuffers(1, &IndexBufferID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IndexBufferID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(IndexBuffer), nullptr, GL_DYNAMIC_DRAW);

    glGenFramebuffers(1, &MainFramebuffer);

    // color buffers
    glGenTextures(1, &ColorBufferTex);
    SetupDefaultTexParams(ColorBufferTex);

    // depth/stencil buffer
    glGenTextures(1, &DepthBufferTex);
    SetupDefaultTexParams(DepthBufferTex);

    // attribute buffer
    // R: opaque polyID (for edgemarking)
    // G: edge flag
    // B: fog flag
    glGenTextures(1, &AttrBufferTex);
    SetupDefaultTexParams(AttrBufferTex);

    Parent.OutputTex3D = ColorBufferTex;

    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

GLRenderer3D::~GLRenderer3D()
{
    assert(glDeleteTextures != nullptr);

    Texcache.Reset();

    glDeleteFramebuffers(1, &MainFramebuffer);
    glDeleteTextures(1, &ColorBufferTex);
    glDeleteTextures(1, &DepthBufferTex);
    glDeleteTextures(1, &AttrBufferTex);

    glDeleteVertexArrays(1, &VertexArrayID);
    glDeleteBuffers(1, &VertexBufferID);
    glDeleteVertexArrays(1, &ClearVertexArrayID);
    glDeleteBuffers(1, &ClearVertexBufferID);
    glDeleteTextures(2, ClearBitmapTex);
    delete[] ClearBitmap[0];
    delete[] ClearBitmap[1];

    glDeleteBuffers(1, &ShaderConfigUBO);

    for (int i = 0; i < 2; i++)
    {
        if (!RenderShader[i]) continue;
        glDeleteProgram(RenderShader[i]);
    }
}

void GLRenderer3D::Reset()
{
    Texcache.Reset();
    ClearBitmapDirty = 0x3;
}

void GLRenderer3D::SetBetterPolygons(bool betterpolygons) noexcept
{
    SetRenderSettings(ScaleFactor, betterpolygons);
}

void GLRenderer3D::SetScaleFactor(int scale) noexcept
{
    SetRenderSettings(scale, BetterPolygons);
}


void GLRenderer3D::SetRenderSettings(int scale, bool betterpolygons) noexcept
{
    if (betterpolygons == BetterPolygons && scale == ScaleFactor)
        return;

    // TODO set it for 2D renderer
    //CurGLCompositor.SetScaleFactor(scale);
    ScaleFactor = scale;
    BetterPolygons = betterpolygons;

    ScreenW = 256 * scale;
    ScreenH = 192 * scale;

    glBindTexture(GL_TEXTURE_2D, ColorBufferTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ScreenW, ScreenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindTexture(GL_TEXTURE_2D, DepthBufferTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, ScreenW, ScreenH, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glBindTexture(GL_TEXTURE_2D, AttrBufferTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ScreenW, ScreenH, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

    GLenum fbassign[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

    glBindFramebuffer(GL_FRAMEBUFFER, MainFramebuffer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ColorBufferTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, DepthBufferTex, 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, AttrBufferTex, 0);
    glDrawBuffers(2, fbassign);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //glLineWidth(scale);
    //glLineWidth(1.5);
}


void GLRenderer3D::SetupPolygon(GLRenderer3D::RendererPolygon* rp, Polygon* polygon) const
{
    rp->PolyData = polygon;

    // render key: depending on what we're drawing
    // opaque polygons:
    // - depthfunc
    // -- alpha=0
    // regular translucent polygons:
    // - depthfunc
    // -- depthwrite
    // --- polyID
    // ---- need opaque
    // shadow mask polygons:
    // - depthfunc?????
    // shadow polygons:
    // - depthfunc
    // -- depthwrite
    // --- polyID

    rp->RenderKey = (polygon->Attr >> 14) & 0x1; // bit14 - depth func
    if (!polygon->IsShadowMask)
    {
        if (polygon->Translucent)
        {
            if (polygon->IsShadow) rp->RenderKey |= 0x20000;
            else                   rp->RenderKey |= 0x10000;
            rp->RenderKey |= (polygon->Attr >> 10) & 0x2; // bit11 - depth write
            rp->RenderKey |= (polygon->Attr >> 13) & 0x4; // bit15 - fog
            rp->RenderKey |= (polygon->Attr & 0x3F000000) >> 16; // polygon ID
            if ((polygon->Attr & 0x001F0000) == 0x001F0000) // need opaque
                rp->RenderKey |= 0x4000;
        }
        else
        {
            if ((polygon->Attr & 0x001F0000) == 0)
                rp->RenderKey |= 0x2;
            rp->RenderKey |= (polygon->Attr & 0x3F000000) >> 16; // polygon ID
        }
    }
    else
    {
        rp->RenderKey |= 0x30000;
    }

    u32 textype = (polygon->TexParam >> 26) & 0x7;
    u32 texattr = (polygon->TexParam >> 16) & 0x3FF;
    if (TexEnable && (textype != 0))
        rp->RenderKey |= (0x80000 | (texattr << 20));
}

u32* GLRenderer3D::SetupVertex(const Polygon* poly, int vid, const Vertex* vtx, u32 vtxattr, u32 texlayer, u32* vptr) const
{
    u32 z = poly->FinalZ[vid];
    u32 w = poly->FinalW[vid];

    u32 alpha = (poly->Attr >> 16) & 0x1F;

    // Z should always fit within 16 bits, so it's okay to do this
    u32 zshift = 0;
    while (z > 0xFFFF) { z >>= 1; zshift++; }

    u32 x, y;
    if (ScaleFactor > 1)
    {
        x = (vtx->HiresPosition[0] * ScaleFactor) >> 4;
        y = (vtx->HiresPosition[1] * ScaleFactor) >> 4;
    }
    else
    {
        x = vtx->FinalPosition[0];
        y = vtx->FinalPosition[1];
    }

    // correct nearly-vertical edges that would look vertical on the DS
    /*{
        int vtopid = vid - 1;
        if (vtopid < 0) vtopid = poly->NumVertices-1;
        Vertex* vtop = poly->Vertices[vtopid];
        if (vtop->FinalPosition[1] >= vtx->FinalPosition[1])
        {
            vtopid = vid + 1;
            if (vtopid >= poly->NumVertices) vtopid = 0;
            vtop = poly->Vertices[vtopid];
        }
        if ((vtop->FinalPosition[1] < vtx->FinalPosition[1]) &&
            (vtx->FinalPosition[0] == vtop->FinalPosition[0]-1))
        {
            if (ScaleFactor > 1)
                x = (vtop->HiresPosition[0] * ScaleFactor) >> 4;
            else
                x = vtop->FinalPosition[0];
        }
    }*/

    *vptr++ = x | (y << 16);
    *vptr++ = z | (w << 16);

    *vptr++ =  (vtx->FinalColor[0] >> 1) |
              ((vtx->FinalColor[1] >> 1) << 8) |
              ((vtx->FinalColor[2] >> 1) << 16) |
              (alpha << 24);

    *vptr++ = (u16)vtx->TexCoords[0] | ((u16)vtx->TexCoords[1] << 16);

    *vptr++ = vtxattr | (zshift << 16);
    *vptr++ = texlayer;
    *vptr++ = TextureWidth(poly->TexParam) | (TextureHeight(poly->TexParam) << 16);

    return vptr;
}

void GLRenderer3D::BuildPolygons(GLRenderer3D::RendererPolygon* polygons, int npolys, int captureinfo[16])
{
    u32* vptr = &VertexBuffer[0];
    u32 vidx = 0;

    u32 iidx = 0;
    u32 eidx = EdgeIndicesOffset;

    u32 curtexparam = 0;
    u32 curtexpal = 0;
    GLuint curtexid = 0;
    u32 curtexlayer = (u32)-1;
    GLuint curhd = 0; int curhdmode = 0;
    HDPending = false;
    const bool hdcards = WC09::IsActive() && WC09::GetHDCardsEnabled();
    const bool hdpack = WC09::IsActive() && WC09::GetHDPackEnabled();
    // keep GPU memory in check: drop HD textures not used for a while (they reload when needed)
    if (++HD3DFrame % 60 == 0 && HD3D.size() > 128)
    {
        for (auto it = HD3D.begin(); it != HD3D.end();)
        {
            if (HD3DFrame - it->second.LastUse > 300)
            {
                if (it->second.Valid) glDeleteTextures(1, &it->second.Tex);
                it = HD3D.erase(it);
            }
            else ++it;
        }
    }

    for (int i = 0; i < npolys; i++)
    {
        RendererPolygon* rp = &polygons[i];
        Polygon* poly = rp->PolyData;

        rp->IndicesOffset = iidx;
        rp->NumIndices = 0;

        u32 vidx_first = vidx;

        u32 polyattr = poly->Attr;
        u32 texparam = poly->TexParam & ~0xC00F0000;
        u32 texpal = poly->TexPalette;

        u32 alpha = (polyattr >> 16) & 0x1F;

        u32 vtxattr = polyattr & 0x1F00C8F0;
        if (poly->FacingView) vtxattr |= (1<<8);
        if (poly->WBuffer)    vtxattr |= (1<<9);

        if ((texparam != curtexparam) || (texpal != curtexpal))
        {
            u32 textype = (texparam >> 26) & 0x7;
            if (TexEnable && (textype != 0))
            {
                // figure out which texture this polygon is going to use

                u32 texaddr = texparam & 0xFFFF;
                u32 texwidth = TextureWidth(texparam);
                u32 texheight = TextureHeight(texparam);
                int capblock = -1;
                if ((textype == 7) && ((texwidth == 128) || (texwidth == 256)))
                {
                    // if this is a direct color texture, and the width is 128 or 256
                    // then it might be a display capture
                    u32 startaddr = texaddr << 3;
                    u32 endaddr = startaddr + (texheight * texwidth * 2);

                    startaddr >>= 15;
                    endaddr = (endaddr + 0x7FFF) >> 15;

                    for (u32 b = startaddr; b < endaddr; b++)
                    {
                        int blk = captureinfo[b];
                        if (blk == -1) continue;

                        capblock = blk;
                    }
                }

                if (capblock != -1)
                {
                    if (texwidth == 128)
                    {
                        curtexid = -1;
                        curtexlayer = capblock | (((texaddr >> 5) & 0x7F) << 20);
                    }
                    else
                    {
                        curtexid = -2;
                        curtexlayer = (capblock >> 2) | (((texaddr >> 6) & 0xFF) << 20);
                    }
                }
                else
                {
                    u32* halp;
                    Texcache.GetTexture(texparam, texpal, curtexid, curtexlayer, halp);
                    curtexlayer |= 0xFFFF0000;
                    if (Texcache.LastCard >= 0)
                    {
                        curhd = 0; curhdmode = 0;
                        if (hdcards)
                        {
                        curhd = GetHDCard(Texcache.LastContentHash, Texcache.LastCard, texwidth, Texcache.LastNewPixels);
                        curhdmode = 1;
                        }
                    }
                    else
                    {
                        curhd = hdpack && !AnimatedTexture(texparam, Texcache.LastContentHash, texwidth, TextureHeight(texparam), textype) ? GetHD3D(Texcache.LastContentHash, Texcache.LastAlphaHash, Texcache.LastHalfHash, Texcache.LastTone, (textype >= 2 && textype <= 4) ? Texcache.LastNewPixels : nullptr, (int)texwidth, (int)TextureHeight(texparam)) : 0;
                        curhdmode = (textype == 1 || textype == 6) ? 2 : 1;
                    }
                }
            }
            else
            {
                // no texture
                curtexid = 0;
                curtexlayer = (u32)-1;
            }

            curtexparam = texparam;
            curtexpal = texpal;
        }

        rp->TexID = curtexid;
        rp->HDTex = (curtexlayer != (u32)-1 && (curtexlayer >> 16) == 0xFFFF) ? curhd : 0;
        rp->HDMode = curhdmode;
        // WC09: duel UI pieces the game animates (phase bar, corner buttons) come in many
        // colour/lighting frames; anything without its own HD image is matched by where it sits
        if (hdpack && !rp->HDTex && curtexlayer != (u32)-1 && (curtexlayer >> 16) == 0xFFFF && ((poly->TexParam >> 26) & 7) == 4)
        {
            u32 tw = TextureWidth(poly->TexParam), th = TextureHeight(poly->TexParam);
            if (tw == 32 && (th == 16 || th == 32))
            {
                int x0 = 999, y0 = 999, x1 = -999, y1 = -999;
                for (u32 v = 0; v < poly->NumVertices; v++)
                {
                    x0 = std::min<int>(x0, poly->Vertices[v]->FinalPosition[0]); x1 = std::max<int>(x1, poly->Vertices[v]->FinalPosition[0]);
                    y0 = std::min<int>(y0, poly->Vertices[v]->FinalPosition[1]); y1 = std::max<int>(y1, poly->Vertices[v]->FinalPosition[1]);
                }
                char name[48] = {};
                char hue = Texcache.LastTone ? (char)(Texcache.LastTone >> 8) : 'n';
                if (th == 16 && y0 == 0 && y1 == 16 && x1 - x0 == 32 && (x0 - 27) % 34 == 0 && x0 >= 27 && x0 <= 197)
                    snprintf(name, sizeof(name), "tex3d/phase_%d_%c", (x0 - 27) / 34, hue);
                else if (th == 32 && x0 == 232 && x1 == 256 && y0 == 0)
                    snprintf(name, sizeof(name), "tex3d/corner_select");
                else if (th == 32 && x0 == 232 && x1 == 256 && y0 == 20)
                    snprintf(name, sizeof(name), "tex3d/corner_page");
                else if (th == 32 && x0 == 0 && x1 == 32 && y0 == 22)
                    snprintf(name, sizeof(name), "tex3d/corner_chain");
                if (name[0])
                {
                    rp->HDTex = GetHDNamed(name);
                    rp->HDMode = 1;
                }
            }
        }
        if (static const bool posdbg = getenv("WC09_POSDBG") != nullptr; posdbg && curtexlayer != (u32)-1 && (curtexlayer >> 16) == 0xFFFF)
        {
            u32 tw = TextureWidth(poly->TexParam), th = TextureHeight(poly->TexParam), tt = (poly->TexParam >> 26) & 7;
            if (tw * th <= 64 * 32)
            {
                int x0 = 999, y0 = 999, x1 = -999, y1 = -999;
                for (u32 v = 0; v < poly->NumVertices; v++)
                {
                    x0 = std::min<int>(x0, poly->Vertices[v]->FinalPosition[0]); x1 = std::max<int>(x1, poly->Vertices[v]->FinalPosition[0]);
                    y0 = std::min<int>(y0, poly->Vertices[v]->FinalPosition[1]); y1 = std::max<int>(y1, poly->Vertices[v]->FinalPosition[1]);
                }
                printf("POS %016llx %ux%u f%u %d,%d-%d,%d hd%d\n", (unsigned long long)Texcache.LastContentHash, tw, th, tt, x0, y0, x1, y1, rp->HDTex ? 1 : 0);
            }
        }
        if (static const char* dbg = getenv("WC09_TEXDBG"); dbg && *dbg && curtexlayer != (u32)-1 &&
            (curtexlayer >> 16) == 0xFFFF && Texcache.LastContentHash == strtoull(dbg, nullptr, 16))
        {
            printf("TEX poly %d:", i);
            for (u32 v = 0; v < poly->NumVertices; v++)
                printf(" (%d,%d uv %.2f,%.2f a%d)", poly->Vertices[v]->FinalPosition[0], poly->Vertices[v]->FinalPosition[1],
                       poly->Vertices[v]->TexCoords[0] / 16.f, poly->Vertices[v]->TexCoords[1] / 16.f, poly->Attr >> 16 & 31);
            printf("\n");
        }
        rp->TexRepeat = (poly->TexParam >> 16) & 0xF;

        // assemble vertices
        if (poly->Type == 1) // line
        {
            rp->PrimType = GL_LINES;

            u32 lastx, lasty;
            int nout = 0;
            for (u32 j = 0; j < poly->NumVertices; j++)
            {
                Vertex* vtx = poly->Vertices[j];

                if (j > 0)
                {
                    if (lastx == vtx->FinalPosition[0] &&
                        lasty == vtx->FinalPosition[1]) continue;
                }

                lastx = vtx->FinalPosition[0];
                lasty = vtx->FinalPosition[1];

                vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, vptr);

                IndexBuffer[iidx++] = vidx;
                rp->NumIndices++;

                vidx++;
                nout++;
                if (nout >= 2) break;
            }
        }
        else if (poly->NumVertices == 3) // regular triangle
        {
            rp->PrimType = GL_TRIANGLES;

            for (int j = 0; j < 3; j++)
            {
                Vertex* vtx = poly->Vertices[j];

                vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, vptr);
                vidx++;
            }

            // build a triangle
            IndexBuffer[iidx++] = vidx_first;
            IndexBuffer[iidx++] = vidx - 2;
            IndexBuffer[iidx++] = vidx - 1;
            rp->NumIndices += 3;
        }
        else // quad, pentagon, etc
        {
            rp->PrimType = GL_TRIANGLES;

            if (!BetterPolygons)
            {
                // regular triangle-splitting

                for (u32 j = 0; j < poly->NumVertices; j++)
                {
                    Vertex* vtx = poly->Vertices[j];

                    vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, vptr);

                    if (j >= 2)
                    {
                        // build a triangle
                        IndexBuffer[iidx++] = vidx_first;
                        IndexBuffer[iidx++] = vidx - 1;
                        IndexBuffer[iidx++] = vidx;
                        rp->NumIndices += 3;
                    }

                    vidx++;
                }
            }
            else
            {
                // attempt at 'better' splitting
                // this doesn't get rid of the error while splitting a bigger polygon into triangles
                // but we can attempt to reduce it

                u32 cX = 0, cY = 0;
                float cZ = 0;
                float cW = 0;

                float cR = 0, cG = 0, cB = 0;
                float cS = 0, cT = 0;

                for (u32 j = 0; j < poly->NumVertices; j++)
                {
                    Vertex* vtx = poly->Vertices[j];

                    cX += vtx->HiresPosition[0];
                    cY += vtx->HiresPosition[1];

                    float fw = (float)poly->FinalW[j] * poly->NumVertices;
                    cW += 1.0f / fw;

                    if (poly->WBuffer) cZ += poly->FinalZ[j] / fw;
                    else               cZ += poly->FinalZ[j];

                    cR += (vtx->FinalColor[0] >> 1) / fw;
                    cG += (vtx->FinalColor[1] >> 1) / fw;
                    cB += (vtx->FinalColor[2] >> 1) / fw;

                    cS += vtx->TexCoords[0] / fw;
                    cT += vtx->TexCoords[1] / fw;
                }

                cX /= poly->NumVertices;
                cY /= poly->NumVertices;

                cW = 1.0f / cW;

                if (poly->WBuffer) cZ *= cW;
                else               cZ /= poly->NumVertices;

                cR *= cW;
                cG *= cW;
                cB *= cW;

                cS *= cW;
                cT *= cW;

                cX = (cX * ScaleFactor) >> 4;
                cY = (cY * ScaleFactor) >> 4;

                u32 w = (u32)cW;

                u32 z = (u32)cZ;
                u32 zshift = 0;
                while (z > 0xFFFF) { z >>= 1; zshift++; }

                // build center vertex
                *vptr++ = cX | (cY << 16);
                *vptr++ = z | (w << 16);

                *vptr++ =  (u32)cR |
                          ((u32)cG << 8) |
                          ((u32)cB << 16) |
                          (alpha << 24);

                *vptr++ = (u16)cS | ((u16)cT << 16);

                *vptr++ = vtxattr | (zshift << 16);
                *vptr++ = curtexlayer;
                *vptr++ = TextureWidth(texparam) | (TextureHeight(texparam) << 16);

                vidx++;

                // build the final polygon
                for (u32 j = 0; j < poly->NumVertices; j++)
                {
                    Vertex* vtx = poly->Vertices[j];

                    vptr = SetupVertex(poly, j, vtx, vtxattr, curtexlayer, vptr);

                    if (j >= 1)
                    {
                        // build a triangle
                        IndexBuffer[iidx++] = vidx_first;
                        IndexBuffer[iidx++] = vidx - 1;
                        IndexBuffer[iidx++] = vidx;
                        rp->NumIndices += 3;
                    }

                    vidx++;
                }

                IndexBuffer[iidx++] = vidx_first;
                IndexBuffer[iidx++] = vidx - 1;
                IndexBuffer[iidx++] = vidx_first + 1;
                rp->NumIndices += 3;
            }
        }

        rp->EdgeIndicesOffset = eidx;
        rp->NumEdgeIndices = 0;

        u32 vidx_cur = vidx_first;
        for (u32 j = 1; j < poly->NumVertices; j++)
        {
            IndexBuffer[eidx++] = vidx_cur;
            IndexBuffer[eidx++] = vidx_cur + 1;
            vidx_cur++;
            rp->NumEdgeIndices += 2;
        }
        IndexBuffer[eidx++] = vidx_cur;
        IndexBuffer[eidx++] = vidx_first;
        rp->NumEdgeIndices += 2;
    }

    NumVertices = vidx;
    NumIndices = iidx;
    NumEdgeIndices = eidx - EdgeIndicesOffset;
}

void GLRenderer3D::SetupPolygonTexture(const RendererPolygon* poly) const
{
    bool iscap = (poly->TexID == (GLuint)-1 || poly->TexID == (GLuint)-2);

    if (iscap)
    {
        if (poly->TexID == (GLuint)-1)
            glActiveTexture(GL_TEXTURE1);
        else
            glActiveTexture(GL_TEXTURE2);
    }
    else
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, poly->TexID);
    }

    GLint repeatS, repeatT;

    if (poly->TexRepeat & (1<<0))
        repeatS = (poly->TexRepeat & (1<<2)) ? GL_MIRRORED_REPEAT : GL_REPEAT;
    else
        repeatS = GL_CLAMP_TO_EDGE;

    if (poly->TexRepeat & (1<<1))
        repeatT = (poly->TexRepeat & (1<<3)) ? GL_MIRRORED_REPEAT : GL_REPEAT;
    else
        repeatT = GL_CLAMP_TO_EDGE;

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, repeatS);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, repeatT);

    // WC09: HD texture replacement
    GLuint hd = iscap ? 0 : poly->HDTex;
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, hd);
    if (hd)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatT);
    }
    glActiveTexture(GL_TEXTURE0);
    if (HDModeULoc >= 0) glUniform1i(HDModeULoc, hd ? poly->HDMode : 0);
}

int GLRenderer3D::RenderSinglePolygon(int i) const
{
    const RendererPolygon* rp = &PolygonList[i];

    SetupPolygonTexture(rp);
    glDrawElements(rp->PrimType, rp->NumIndices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));

    return 1;
}

int GLRenderer3D::RenderPolygonBatch(int i) const
{
    const RendererPolygon* rp = &PolygonList[i];
    GLuint primtype = rp->PrimType;
    u32 renderkey = rp->RenderKey;
    GLuint texid = rp->TexID;
    u32 texrepeat = rp->TexRepeat;
    int numpolys = 0;
    u32 numindices = 0;

    for (int iend = i; iend < NumFinalPolys; iend++)
    {
        const RendererPolygon* cur_rp = &PolygonList[iend];
        if (cur_rp->PrimType != primtype) break;
        if (cur_rp->RenderKey != renderkey) break;
        if (cur_rp->TexID != texid) break;
        if (cur_rp->TexRepeat != texrepeat) break;
        if (cur_rp->HDTex != rp->HDTex) break;

        numpolys++;
        numindices += cur_rp->NumIndices;
    }

    SetupPolygonTexture(rp);
    glDrawElements(primtype, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->IndicesOffset * 2));
    return numpolys;
}

int GLRenderer3D::RenderPolygonEdgeBatch(int i) const
{
    const RendererPolygon* rp = &PolygonList[i];
    u32 renderkey = rp->RenderKey;
    GLuint texid = rp->TexID;
    u32 texrepeat = rp->TexRepeat;
    int numpolys = 0;
    u32 numindices = 0;

    for (int iend = i; iend < NumFinalPolys; iend++)
    {
        const RendererPolygon* cur_rp = &PolygonList[iend];
        if (cur_rp->RenderKey != renderkey) break;
        if (cur_rp->TexID != texid) break;
        if (cur_rp->TexRepeat != texrepeat) break;
        if (cur_rp->HDTex != rp->HDTex) break;

        numpolys++;
        numindices += cur_rp->NumEdgeIndices;
    }

    SetupPolygonTexture(rp);
    glDrawElements(GL_LINES, numindices, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(rp->EdgeIndicesOffset * 2));
    return numpolys;
}

void GLRenderer3D::RenderSceneChunk(int y, int h)
{
    bool flags = GPU3D.RenderPolygonRAM[0]->WBuffer;
    UseRenderShader(flags);

    //if (h != 192) glScissor(0, y<<ScaleFactor, 256<<ScaleFactor, h<<ScaleFactor);

    GLboolean fogenable = (GPU3D.RenderDispCnt & (1<<7)) ? GL_TRUE : GL_FALSE;

    // TODO: proper 'equal' depth test!
    // (has margin of +-0x200 in Z-buffer mode, +-0xFF in W-buffer mode)
    // for now we're using GL_LEQUAL to make it work to some extent

    // STENCIL BUFFER VALUES
    // 1111'1111 : background (clear plane)
    // 1111'1110 : shadow mask against background
    // 00pp'pppp : opaque polygon ID p
    // 01pp'pppp : translucent polygon ID p
    // 1___'____ : shadow mask

    // POLYGON ID BASED RENDERING RULES
    // opaque polygons: polygon ID ignored
    // translucent polygons: if dst is opaque
    //                       OR if dst is translucent and polygon ID is different
    // shadow polygons: if (dst is opaque AND dst opaque polygon ID is different)
    //                  OR (dst is translucent and dst translucent polygon ID is different)

    // pass 1: opaque pixels

    glUniform1i(RenderModeULoc, RenderMode_Opaque);
    glLineWidth(1.0);

    glColorMaski(1, GL_TRUE, GL_TRUE, fogenable, GL_FALSE);

    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    glBindVertexArray(VertexArrayID);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput128Tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, Parent.CaptureOutput256Tex);

    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < NumFinalPolys; )
    {
        RendererPolygon* rp = &PolygonList[i];

        if (rp->PolyData->IsShadowMask) { i++; continue; }
        if (rp->PolyData->Translucent) { i++; continue; }

        if (rp->PolyData->Attr & (1<<14))
            glDepthFunc(GL_LEQUAL);
        else
            glDepthFunc(GL_LESS);

        u32 polyattr = rp->PolyData->Attr;
        u32 polyid = (polyattr >> 24) & 0x3F;

        glStencilFunc(GL_ALWAYS, polyid, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0xFF);

        i += RenderPolygonBatch(i);
    }

    // if edge marking is enabled, mark all opaque edges
    // TODO BETTER EDGE MARKING!!! THIS SUCKS
    /*if (RenderDispCnt & (1<<5))
    {
        UseRenderShader(flags | RenderFlag_Edge);
        glLineWidth(1.5);

        glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glColorMaski(1, GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);

        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_FALSE);

        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilMask(0);

        for (int i = 0; i < NumFinalPolys; )
        {
            RendererPolygon* rp = &PolygonList[i];

            if (rp->PolyData->IsShadowMask) { i++; continue; }

            i += RenderPolygonEdgeBatch(i);
        }

        glDepthMask(GL_TRUE);
    }*/

    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);

    if (GPU3D.RenderDispCnt & (1<<3))
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
    else
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ONE);

    glLineWidth(1.0);

    if (NumOpaqueFinalPolys > -1)
    {
        // pass 2: if needed, render translucent pixels that are against background pixels
        // when background alpha is zero, those need to be rendered with blending disabled

        if ((GPU3D.RenderClearAttr1 & 0x001F0000) == 0)
        {
            glDisable(GL_BLEND);

            for (int i = 0; i < NumFinalPolys; )
            {
                RendererPolygon* rp = &PolygonList[i];

                if (rp->PolyData->IsShadowMask)
                {
                    // draw actual shadow mask

                    glUniform1i(RenderModeULoc, RenderMode_ShadowMask);

                    glDisable(GL_BLEND);
                    glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glDepthMask(GL_FALSE);

                    // render where stencil is 0xFF
                    // set to 0xFE where this polygon z-fails
                    glDepthFunc(GL_LESS);
                    glStencilFunc(GL_EQUAL, 0xFF, 0xFF);
                    glStencilOp(GL_KEEP, GL_INVERT, GL_KEEP);
                    glStencilMask(0x01);

                    i += RenderPolygonBatch(i);
                }
                else if (rp->PolyData->Translucent)
                {
                    bool needopaque = ((rp->PolyData->Attr & 0x001F0000) == 0x001F0000);

                    u32 polyattr = rp->PolyData->Attr;
                    u32 polyid = (polyattr >> 24) & 0x3F;

                    if (polyattr & (1<<14))
                        glDepthFunc(GL_LEQUAL);
                    else
                        glDepthFunc(GL_LESS);

                    if (needopaque)
                    {
                        glUniform1i(RenderModeULoc, RenderMode_Opaque);

                        glDisable(GL_BLEND);
                        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glColorMaski(1, GL_TRUE, GL_TRUE, fogenable, GL_FALSE);

                        // set stencil to the polygon's ID
                        glStencilFunc(GL_ALWAYS, polyid, 0xFF);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                        glStencilMask(0xFF);

                        glDepthMask(GL_TRUE);

                        RenderSinglePolygon(i);
                    }

                    glUniform1i(RenderModeULoc, RenderMode_Translucent);

                    GLboolean transfog;
                    if (!(polyattr & (1<<15))) transfog = fogenable;
                    else                       transfog = GL_FALSE;

                    if (rp->PolyData->IsShadow)
                    {
                        // shadow against clear-plane will only pass if its polyID matches that of the clear plane
                        u32 clrpolyid = (GPU3D.RenderClearAttr1 >> 24) & 0x3F;
                        if (polyid != clrpolyid) { i++; continue; }

                        glEnable(GL_BLEND);
                        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                        // draw where shadow mask has previously been rendered (stencil=0xFE)
                        // when passing, set it to (polyID | 0x40)
                        // TODO might break bit0 of polyID
                        glStencilFunc(GL_EQUAL, 0xFE, 0xFF);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
                        glStencilMask(~(0x40|polyid)); // heheh

                        if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                        else                    glDepthMask(GL_FALSE);

                        i += needopaque ? RenderSinglePolygon(i) : RenderPolygonBatch(i);
                    }
                    else
                    {
                        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                        // draw on either background (0xFF) or shadowmask (0xFE)
                        // when passing, set it to (polyID | 0x40)
                        glStencilFunc(GL_EQUAL, 0xFF, 0xFE);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
                        glStencilMask(~(0x40|polyid)); // heheh

                        if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                        else                    glDepthMask(GL_FALSE);

                        i += needopaque ? RenderSinglePolygon(i) : RenderPolygonBatch(i);
                    }
                }
                else
                    i++;
            }

            glEnable(GL_BLEND);
            glStencilMask(0xFF);
        }

        // pass 3: translucent pixels

        for (int i = 0; i < NumFinalPolys; )
        {
            RendererPolygon* rp = &PolygonList[i];

            if (rp->PolyData->IsShadowMask)
            {
                // clear shadow bits in stencil buffer

                glStencilMask(0x80);
                glClear(GL_STENCIL_BUFFER_BIT);

                // draw actual shadow mask

                glUniform1i(RenderModeULoc, RenderMode_ShadowMask);

                glDisable(GL_BLEND);
                glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_FALSE);

                // set stencil bit7 where the shadowmask z-fails
                glDepthFunc(GL_LESS);
                glStencilFunc(GL_ALWAYS, 0x80, 0x80);
                glStencilOp(GL_KEEP, GL_REPLACE, GL_KEEP);

                i += RenderPolygonBatch(i);
            }
            else if (rp->PolyData->Translucent)
            {
                bool needopaque = ((rp->PolyData->Attr & 0x001F0000) == 0x001F0000);

                u32 polyattr = rp->PolyData->Attr;
                u32 polyid = (polyattr >> 24) & 0x3F;

                if (polyattr & (1<<14))
                    glDepthFunc(GL_LEQUAL);
                else
                    glDepthFunc(GL_LESS);

                if (needopaque)
                {
                    glUniform1i(RenderModeULoc, RenderMode_Opaque);

                    glDisable(GL_BLEND);
                    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glColorMaski(1, GL_TRUE, GL_TRUE, fogenable, GL_FALSE);

                    // set stencil to polyID
                    glStencilFunc(GL_ALWAYS, polyid, 0xFF);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    glStencilMask(0xFF);

                    glDepthMask(GL_TRUE);

                    RenderSinglePolygon(i);
                }

                glUniform1i(RenderModeULoc, RenderMode_Translucent);

                GLboolean transfog;
                if (!(polyattr & (1<<15))) transfog = fogenable;
                else                       transfog = GL_FALSE;

                if (rp->PolyData->IsShadow)
                {
                    glDisable(GL_BLEND);
                    glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    glDepthMask(GL_FALSE);

                    // render where polyID matches (ignoring other bits)
                    // clear bit7 where it passes
                    glStencilFunc(GL_EQUAL, polyid, 0x3F);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
                    glStencilMask(0x80);

                    RenderSinglePolygon(i);

                    glEnable(GL_BLEND);
                    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                    // render where bit7 is set (ie. shadow mask)
                    // set bit6 and replace polyID
                    glStencilFunc(GL_EQUAL, 0xC0|polyid, 0x80);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    glStencilMask(0x7F);

                    if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                    else                    glDepthMask(GL_FALSE);

                    i += RenderSinglePolygon(i);
                }
                else
                {
                    glEnable(GL_BLEND);
                    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE);

                    // render where polyID and bit6 do not match
                    // set bit6 and set polyID
                    glStencilFunc(GL_NOTEQUAL, 0x40|polyid, 0x7F);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    glStencilMask(0x7F);

                    if (polyattr & (1<<11)) glDepthMask(GL_TRUE);
                    else                    glDepthMask(GL_FALSE);

                    i += needopaque ? RenderSinglePolygon(i) : RenderPolygonBatch(i);
                }
            }
            else
                i++;
        }
    }

    if (GPU3D.RenderDispCnt & 0x00A0) // fog/edge enabled
    {
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_FALSE);
        glStencilFunc(GL_ALWAYS, 0, 0);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilMask(0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, DepthBufferTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, AttrBufferTex);

        glBindBuffer(GL_ARRAY_BUFFER, ClearVertexBufferID);
        glBindVertexArray(ClearVertexArrayID);

        if (GPU3D.RenderDispCnt & (1<<5))
        {
            // edge marking
            // TODO: depth/polyid values at screen edges

            glUseProgram(FinalPassEdgeShader);

            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

            glDrawArrays(GL_TRIANGLES, 0, 2*3);
        }

        if (GPU3D.RenderDispCnt & (1<<7))
        {
            // fog

            glUseProgram(FinalPassFogShader);

            if (GPU3D.RenderDispCnt & (1<<6))
                glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            else
                glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA);

            {
                u32 c = GPU3D.RenderFogColor;
                u32 r = c & 0x1F;
                u32 g = (c >> 5) & 0x1F;
                u32 b = (c >> 10) & 0x1F;
                u32 a = (c >> 16) & 0x1F;

                glBlendColor((float)r/31.0, (float)g/31.0, (float)b/31.0, (float)a/31.0);
            }

            glDrawArrays(GL_TRIANGLES, 0, 2*3);
        }
    }
}


void GLRenderer3D::RenderFrame()
{
    WC09Perf::Scope perf(&WC09Perf::Render3DUs);
    WC09Perf::Hd3DCount = (int)HD3D.size();
    // WC09: this renderer draws HD cards itself, even on frames that reuse the last scene
    if (WC09::IsActive() && WC09::GetHDCardsEnabled()) WC09::NoteCards3DRendered();
    u8 clrBitmapDirty;
    // a changed HD switch must show at once, even on a scene the game doesn't redraw
    u32 hdgen = WC09::HDSettingsGeneration();
    bool hdChanged = hdgen != LastHDGen;
    // decode the textures again: HD cards are built from freshly decoded card textures
    if (hdChanged && LastHDGen != 0) Texcache.Reset();
    LastHDGen = hdgen;
    // HD pictures still loading last time: draw again until they're all in
    bool hdWaiting = HDPending;
    if (!Texcache.Update(clrBitmapDirty) && GPU3D.RenderFrameIdentical && !hdChanged && !hdWaiting)
    {
        return;
    }

    // figure out which chunks of texture memory contain display captures
    int captureinfo[16];
    GPU.GetCaptureInfo_Texture(captureinfo);

    // if we're using a clear bitmap, set that up
    ClearBitmapDirty |= clrBitmapDirty;
    if (GPU3D.RenderDispCnt & (1<<14))
    {
        if (ClearBitmapDirty & (1<<0))
        {
            u16* vram = (u16*)&GPU.VRAMFlat_Texture[0x40000];
            for (int i = 0; i < 256*256; i++)
            {
                u16 color = vram[i];
                u32 r = (color << 1) & 0x3E; if (r) r++;
                u32 g = (color >> 4) & 0x3E; if (g) g++;
                u32 b = (color >> 9) & 0x3E; if (b) b++;
                u32 a = (color & 0x8000) ? 31 : 0;

                ClearBitmap[0][i] = r | (g << 8) | (b << 16) | (a << 24);
            }

            glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[0]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, ClearBitmap[0]);
        }

        if (ClearBitmapDirty & (1<<1))
        {
            u16* vram = (u16*)&GPU.VRAMFlat_Texture[0x60000];
            for (int i = 0; i < 256*256; i++)
            {
                u16 val = vram[i];
                u32 depth = ((val & 0x7FFF) * 0x200) + 0x1FF;
                u32 fog = (val & 0x8000) << 9;

                ClearBitmap[1][i] = depth | fog;
            }

            glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[1]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RED_INTEGER, GL_UNSIGNED_INT, ClearBitmap[1]);
        }

        ClearBitmapDirty = 0;
    }

    TexEnable = !!(GPU3D.RenderDispCnt & (1<<0));

    CurShaderID = -1;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, MainFramebuffer);

    ShaderConfig.uScreenSize[0] = ScreenW;
    ShaderConfig.uScreenSize[1] = ScreenH;
    ShaderConfig.uDispCnt = GPU3D.RenderDispCnt;

    for (int i = 0; i < 32; i++)
    {
        u16 c = GPU3D.RenderToonTable[i];
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;

        ShaderConfig.uToonColors[i][0] = (float)r / 31.0;
        ShaderConfig.uToonColors[i][1] = (float)g / 31.0;
        ShaderConfig.uToonColors[i][2] = (float)b / 31.0;
    }

    for (int i = 0; i < 8; i++)
    {
        u16 c = GPU3D.RenderEdgeTable[i];
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;

        ShaderConfig.uEdgeColors[i][0] = (float)r / 31.0;
        ShaderConfig.uEdgeColors[i][1] = (float)g / 31.0;
        ShaderConfig.uEdgeColors[i][2] = (float)b / 31.0;
    }

    {
        u32 c = GPU3D.RenderFogColor;
        u32 r = c & 0x1F;
        u32 g = (c >> 5) & 0x1F;
        u32 b = (c >> 10) & 0x1F;
        u32 a = (c >> 16) & 0x1F;

        ShaderConfig.uFogColor[0] = (float)r / 31.0;
        ShaderConfig.uFogColor[1] = (float)g / 31.0;
        ShaderConfig.uFogColor[2] = (float)b / 31.0;
        ShaderConfig.uFogColor[3] = (float)a / 31.0;
    }

    for (int i = 0; i < 34; i++)
    {
        u8 d = GPU3D.RenderFogDensityTable[i];
        ShaderConfig.uFogDensity[i][0] = (float)d / 127.0;
    }

    ShaderConfig.uFogOffset = GPU3D.RenderFogOffset;
    ShaderConfig.uFogShift = GPU3D.RenderFogShift;

    glBindBuffer(GL_UNIFORM_BUFFER, ShaderConfigUBO);
    void* unibuf = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
    if (unibuf) memcpy(unibuf, &ShaderConfig, sizeof(ShaderConfig));
    glUnmapBuffer(GL_UNIFORM_BUFFER);

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);

    glViewport(0, 0, ScreenW, ScreenH);

    glDisable(GL_BLEND);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);

    glDepthFunc(GL_ALWAYS);
    glStencilFunc(GL_ALWAYS, 0xFF, 0xFF);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

    // clear buffers
    // TODO: check whether 'clear polygon ID' affects translucent polyID
    // (for example when alpha is 1..30)
    if (GPU3D.RenderDispCnt & (1<<14))
    {
        // clear bitmap
        glUseProgram(ClearShaderBitmap);

        u32 polyid = (GPU3D.RenderClearAttr1 >> 24) & 0x3F;

        float bitmapoffset[2];
        u8 xoff = (GPU3D.RenderClearAttr2 >> 16) & 0xFF;
        u8 yoff = (GPU3D.RenderClearAttr2 >> 24) & 0xFF;
        bitmapoffset[0] = (float)xoff / 256.0;
        bitmapoffset[1] = (float)yoff / 256.0;

        glUniform2f(ClearBitmapULoc[0], bitmapoffset[0], bitmapoffset[1]);
        glUniform1ui(ClearBitmapULoc[1], polyid);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[0]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ClearBitmapTex[1]);
    }
    else
    {
        // plain clear plane
        glUseProgram(ClearShaderPlain);

        u32 r = GPU3D.RenderClearAttr1 & 0x1F;
        u32 g = (GPU3D.RenderClearAttr1 >> 5) & 0x1F;
        u32 b = (GPU3D.RenderClearAttr1 >> 10) & 0x1F;
        u32 fog = (GPU3D.RenderClearAttr1 >> 15) & 0x1;
        u32 a = (GPU3D.RenderClearAttr1 >> 16) & 0x1F;
        u32 polyid = (GPU3D.RenderClearAttr1 >> 24) & 0x3F;
        u32 z = ((GPU3D.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;

        /*if (r) r = r*2 + 1;
        if (g) g = g*2 + 1;
        if (b) b = b*2 + 1;*/

        glUniform4ui(ClearUniformLoc[0], r, g, b, a);
        glUniform1ui(ClearUniformLoc[1], z);
        glUniform1ui(ClearUniformLoc[2], polyid);
        glUniform1ui(ClearUniformLoc[3], fog);
    }

    glBindBuffer(GL_ARRAY_BUFFER, ClearVertexBufferID);
    glBindVertexArray(ClearVertexArrayID);
    glDrawArrays(GL_TRIANGLES, 0, 2*3);

    if (GPU3D.RenderNumPolygons)
    {
        int npolys = 0;
        int firsttrans = -1;
        for (u32 i = 0; i < GPU3D.RenderNumPolygons; i++)
        {
            if (GPU3D.RenderPolygonRAM[i]->Degenerate) continue;

            SetupPolygon(&PolygonList[npolys], GPU3D.RenderPolygonRAM[i]);
            if (firsttrans < 0 && GPU3D.RenderPolygonRAM[i]->Translucent)
                firsttrans = npolys;

            npolys++;
        }
        NumFinalPolys = npolys;
        NumOpaqueFinalPolys = firsttrans;

        BuildPolygons(&PolygonList[0], npolys, captureinfo);
        glBindBuffer(GL_ARRAY_BUFFER, VertexBufferID);
        glBufferSubData(GL_ARRAY_BUFFER, 0, NumVertices*7*4, VertexBuffer);

        // bind to access the index buffer
        glBindVertexArray(VertexArrayID);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, NumIndices * 2, IndexBuffer);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, EdgeIndicesOffset * 2, NumEdgeIndices * 2, IndexBuffer + EdgeIndicesOffset);

        RenderSceneChunk(0, 192);
    }
}

u32* GLRenderer3D::GetLine(int line)
{
    return nullptr;
}

}
