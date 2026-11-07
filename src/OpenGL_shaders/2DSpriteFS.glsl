#version 140

uniform sampler2D SpriteTex;
uniform sampler2DArray Capture128Tex;
uniform sampler2DArray Capture256Tex;

struct sOAM
{
    ivec2 Position;
    bvec2 Flip;
    ivec2 Size;
    ivec2 BoundSize;
    int OBJMode;
    int Type;
    int PalOffset;
    int TileOffset;
    int TileStride;
    int Rotscale;
    int BGPrio;
    bool Mosaic;
};

layout(std140) uniform ubSpriteConfig
{
    int uVRAMMask;
    ivec4 uRotscale[32];
    sOAM uOAM[128];
};

layout(std140) uniform ubSpriteScanlineConfig
{
    ivec4 uMosaicLine[48];
};

uniform bool uRenderTransparent;

flat in int fSpriteIndex;
smooth in vec2 fPosition;
smooth in vec2 fTexcoord;

out vec4 oColor;
out vec4 oFlags;


// ---- WC09: xBR-lv2 style edge-directed upscaling (after Hyllian's xBR, MIT) ----
uniform int uSmoothScale;   // 0 = off, else the renderer's scale factor

const vec3 kXbrY = vec3(0.2126, 0.7152, 0.0722) * 48.0;

float XbrLum(vec4 c) { return dot(c.rgb, kXbrY) + c.a * 24.0; }   // alpha edges count as edges too

vec4 XbrDF(vec4 a, vec4 b) { return abs(a - b); }
bvec4 XbrEQ(vec4 a, vec4 b) { return lessThan(XbrDF(a, b), vec4(10.0)); }
bvec4 XbrNEQ(vec4 a, vec4 b) { return not(XbrEQ(a, b)); }
vec4 XbrWD(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h)
{
    return XbrDF(a, b) + XbrDF(a, c) + XbrDF(d, e) + XbrDF(d, f) + 4.0 * XbrDF(g, h);
}
float XbrCDF(vec4 a, vec4 b) { vec4 d = abs(a - b); return d.r + d.g + d.b + d.a; }


vec4 XbrResolve(vec4 A1, vec4 B1, vec4 C1, vec4 A, vec4 B, vec4 C, vec4 D, vec4 E, vec4 F,
                vec4 G, vec4 H, vec4 I, vec4 G5, vec4 H5, vec4 I5, vec4 A0, vec4 D0, vec4 G0,
                vec4 C4, vec4 F4, vec4 I4, vec2 fp, float sc)
{
    vec4 b = vec4(XbrLum(B), XbrLum(D), XbrLum(H), XbrLum(F));
    vec4 c = vec4(XbrLum(C), XbrLum(A), XbrLum(G), XbrLum(I));
    vec4 e = vec4(XbrLum(E));
    vec4 d = b.yzwx, f = b.wxyz, g = c.zwxy, h = b.zwxy, i = c.wxyz;
    vec4 i4 = vec4(XbrLum(I4), XbrLum(C1), XbrLum(A0), XbrLum(G5));
    vec4 i5 = vec4(XbrLum(I5), XbrLum(C4), XbrLum(A1), XbrLum(G0));
    vec4 h5 = vec4(XbrLum(H5), XbrLum(F4), XbrLum(B1), XbrLum(D0));
    vec4 f4 = h5.yzwx;

    const vec4 Ao = vec4(1.0, -1.0, -1.0, 1.0), Bo = vec4(1.0, 1.0, -1.0, -1.0), Co = vec4(1.5, 0.5, -0.5, 0.5);
    const vec4 Ax = vec4(1.0, -1.0, -1.0, 1.0), Bx = vec4(0.5, 2.0, -0.5, -2.0), Cx = vec4(1.0, 1.0, -0.5, 0.0);
    const vec4 Ay = vec4(1.0, -1.0, -1.0, 1.0), By = vec4(2.0, 0.5, -2.0, -0.5), Cy = vec4(2.0, 0.0, -1.0, 0.5);

    vec4 fx      = Ao * fp.y + Bo * fp.x;
    vec4 fx_left = Ax * fp.y + Bx * fp.x;
    vec4 fx_up   = Ay * fp.y + By * fp.x;

    bvec4 irlv0 = bvec4(vec4(XbrNEQ(e, f)) * vec4(XbrNEQ(e, h)));
    bvec4 irlv1 = bvec4(vec4(irlv0) * clamp(
        vec4(XbrNEQ(f, b)) * vec4(XbrNEQ(h, d)) +
        vec4(XbrEQ(e, i)) * vec4(XbrNEQ(f, i4)) * vec4(XbrNEQ(h, i5)) +
        vec4(XbrEQ(e, g)) + vec4(XbrEQ(e, c)), 0.0, 1.0));
    bvec4 irlv2_left = bvec4(vec4(XbrNEQ(e, g)) * vec4(XbrNEQ(d, g)));
    bvec4 irlv2_up   = bvec4(vec4(XbrNEQ(e, c)) * vec4(XbrNEQ(b, c)));

    vec4 delta   = vec4(1.0 / sc);
    vec4 delta_l = vec4(0.5 / sc, 1.0 / sc, 0.5 / sc, 1.0 / sc);
    vec4 delta_u = delta_l.yxwz;

    vec4 fx45 = smoothstep(Co - delta,   Co + delta,   fx);
    vec4 fx30 = smoothstep(Cx - delta_l, Cx + delta_l, fx_left);
    vec4 fx60 = smoothstep(Cy - delta_u, Cy + delta_u, fx_up);

    bvec4 edr = bvec4(vec4(lessThan(XbrWD(e, c, g, i, h5, f4, h, f), XbrWD(h, d, i5, f, i4, b, e, i))) * vec4(irlv1));
    bvec4 edr_left = bvec4(vec4(lessThanEqual(2.0 * XbrDF(f, g), XbrDF(h, c))) * vec4(irlv2_left) * vec4(edr));
    bvec4 edr_up   = bvec4(vec4(greaterThanEqual(XbrDF(f, g), 2.0 * XbrDF(h, c))) * vec4(irlv2_up) * vec4(edr));

    fx45 *= vec4(edr); fx30 *= vec4(edr_left); fx60 *= vec4(edr_up);
    bvec4 px = lessThanEqual(XbrDF(e, f), XbrDF(e, h));
    vec4 maximo = max(max(fx30, fx60), fx45);

    vec4 res1 = mix(E, mix(H, F, float(px.x)), maximo.x);
    vec4 res2 = mix(E, mix(B, D, float(px.z)), maximo.z);
    res1 = mix(res1, res2, step(XbrCDF(E, res1), XbrCDF(E, res2)));
    vec4 res3 = mix(E, mix(D, B, float(px.y)), maximo.y);
    vec4 res4 = mix(E, mix(F, H, float(px.w)), maximo.w);
    res2 = mix(res3, res4, step(XbrCDF(E, res3), XbrCDF(E, res4)));
    vec4 res = mix(res1, res2, step(XbrCDF(E, res1), XbrCDF(E, res2)));

    // soft areas (no strong edge around E): smooth bilinear instead of blocky nearest.
    // Keeps xBR's crisp edges on lines and text, and smooths painted shading.
    if (all(lessThan(abs(e - b), vec4(13.0))) && all(lessThan(abs(e - c), vec4(18.0))))
    {
        vec2 o = fp - 0.5;
        vec4 nx = (o.x < 0.0) ? D : F;
        vec4 ny = (o.y < 0.0) ? B : H;
        vec4 nxy = (o.x < 0.0) ? ((o.y < 0.0) ? A : G) : ((o.y < 0.0) ? C : I);
        res = mix(mix(E, nx, abs(o.x)), mix(ny, nxy, abs(o.x)), abs(o.y));
    }

    // back to straight alpha; the compositor treats alpha as on/off
    if (res.a < 0.5) return vec4(0.0);
    return vec4(res.rgb / res.a, 1.0);
}

ivec3 ConvertColor(int col)
{
    ivec3 ret;
    ret.r = (col & 0x1F) << 1;
    ret.g = ((col & 0x3E0) >> 4) | (col >> 15);
    ret.b = (col & 0x7C00) >> 9;
    return ret;
}

vec4 XbrTexelSprite(ivec2 base, ivec2 size, ivec2 p)
{
    p = clamp(p, ivec2(0), size - 1);
    vec4 c = texelFetch(SpriteTex, base + p, 0);
    return vec4(c.rgb * c.a, c.a);
}

vec4 XbrFetchSprite(int sprite, vec2 coord)
{
    ivec2 base = ivec2((sprite & 0xF) * 64, (sprite >> 4) * 64);
    ivec2 size = uOAM[sprite].Size;
    vec2 pos = coord;
    vec2 fp = fract(pos);
    ivec2 p = ivec2(floor(pos));
#define T(dx, dy) XbrTexelSprite(base, size, p + ivec2(dx, dy))
    vec4 r = XbrResolve(T(-1,-2), T(0,-2), T(1,-2), T(-1,-1), T(0,-1), T(1,-1), T(-1,0), T(0,0), T(1,0),
                        T(-1,1), T(0,1), T(1,1), T(-1,2), T(0,2), T(1,2), T(-2,-1), T(-2,0), T(-2,1),
                        T(2,-1), T(2,0), T(2,1), fp, float(uSmoothScale));
#undef T
    return r;
}

// ---- WC09: HD sprite images (portraits, character figures, menu objects) ----
// a uniform block: 256 vectors would be over the 224 plain uniform vectors some GPUs allow (iPhone)
layout(std140) uniform ubSprHD
{
    ivec4 uSprHDA[128];     // slot (-1 = none), image x/y of the matched OAM, flags (1 = hflip, 2 = vflip)
    ivec4 uSprHDB[128];     // matched OAM w/h, tile-space offset x/y
};
uniform vec2 uSprHDSize[13];     // image size in original pixels
uniform sampler2D SprHDTex[13];

vec4 SprHDSample(int slot, vec2 uv)
{
    if (slot == 0) return texture(SprHDTex[0], uv);
    if (slot == 1) return texture(SprHDTex[1], uv);
    if (slot == 2) return texture(SprHDTex[2], uv);
    if (slot == 3) return texture(SprHDTex[3], uv);
    if (slot == 4) return texture(SprHDTex[4], uv);
    if (slot == 5) return texture(SprHDTex[5], uv);
    if (slot == 6) return texture(SprHDTex[6], uv);
    if (slot == 7) return texture(SprHDTex[7], uv);
    if (slot == 8) return texture(SprHDTex[8], uv);
    if (slot == 9) return texture(SprHDTex[9], uv);
    if (slot == 10) return texture(SprHDTex[10], uv);
    if (slot == 11) return texture(SprHDTex[11], uv);
    return texture(SprHDTex[12], uv);
}


// WC09: anti-aliased text sprites: colours interpolated with a cubic filter, coverage cut with a
// soft threshold. Gives smooth curved letters instead of enlarged pixels.
vec4 CatmullRomW(float t)
{
    float t2 = t * t, t3 = t2 * t;
    return vec4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);
}

vec4 TextFetchSprite(int sprite, vec2 coord)
{
    ivec2 base = ivec2((sprite & 0xF) * 64, (sprite >> 4) * 64);
    ivec2 size = uOAM[sprite].Size;
    vec2 pos = coord - 0.5;
    ivec2 p = ivec2(floor(pos));
    vec2 f = pos - vec2(p);
    vec4 wx = CatmullRomW(f.x), wy = CatmullRomW(f.y);
    vec4 acc = vec4(0.0);
    float near = 0.0; vec3 nearc = vec3(0.0);
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
        {
            ivec2 q = clamp(p + ivec2(i - 1, j - 1), ivec2(0), size - 1);
            vec4 c = texelFetch(SpriteTex, base + q, 0);
            float w = wx[i] * wy[j];
            acc += w * vec4(c.rgb * c.a, c.a);
            if (i >= 1 && i <= 2 && j >= 1 && j <= 2 && c.a > near) { near = c.a; nearc = c.rgb; }
        }
    float a = smoothstep(0.35, 0.65, acc.a);
    if (a < 0.5) return vec4(0.0);
    vec3 col = acc.a > 0.01 ? clamp(acc.rgb / acc.a, 0.0, 1.0) : nearc;
    return vec4(col, 1.0);
}

vec4 GetSpritePixel(int sprite, vec2 coord)
{
    ivec4 hda = uSprHDA[sprite];
    if (hda.x >= 0)
    {
        ivec4 hdb = uSprHDB[sprite];
        vec2 s = coord + vec2(hdb.zw);
        vec2 c = vec2(((hda.w & 1) != 0) ? float(hdb.x) - s.x : s.x,
                      ((hda.w & 2) != 0) ? float(hdb.y) - s.y : s.y);
        vec2 uv = (vec2(hda.yz) + c) / uSprHDSize[hda.x];
        vec4 h = SprHDSample(hda.x, uv);
        // alpha ~0.3 in an HD image = "show the game's own pixels here" (palette-driven parts such
        // as selection outlines that the HD picture can't know about)
        bool passthru = h.a > 0.15 && h.a < 0.45;
        if (h.a < 0.5 && !passthru) return vec4(0.0);
        if ((hda.w & 16) != 0) return passthru ? vec4(0.0) : vec4(h.rgb, 1.0);
        // only where the game actually shows pixels (within 1 px): parts of an image the game
        // leaves blank or cuts off stay blank
        ivec2 base = ivec2((sprite & 0xF) * 64, (sprite >> 4) * 64);
        ivec2 ic = ivec2(coord), sz = uOAM[sprite].Size;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                ivec2 p = ic + ivec2(dx, dy);
                if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, sz))) continue;
                if (texelFetch(SpriteTex, base + p, 0).a > 0.0) { if (!passthru) return vec4(h.rgb, 1.0); dy = 2; break; }
            }
        if (!passthru) return vec4(0.0);
    }

    if (uSmoothScale > 1 && (hda.w & 4) != 0)
        return TextFetchSprite(sprite, coord);
    if (uSmoothScale > 1 && uOAM[sprite].OBJMode != 2)
        return XbrFetchSprite(sprite, coord);
    ivec2 basecoord = ivec2((sprite & 0xF) * 64, (sprite >> 4) * 64);

    return texelFetch(SpriteTex, basecoord + ivec2(coord), 0);
}

void main()
{
    vec4 col, flags = vec4(0);
    vec2 coord = fTexcoord;

    // WC09: portrait edge columns under the HD life-point panel (WC09Battle.h kLpX/kLpY/kLpW)
    if ((uSprHDA[fSpriteIndex].w & 8) != 0 && fPosition.y >= 151.0 && fPosition.y < 192.0 &&
        (int(floor(fPosition.x)) == 45 || int(floor(fPosition.x)) == 207))
        discard;

    if (uOAM[fSpriteIndex].Mosaic)
    {
        int line = int(fPosition.y);
        int mosline = uMosaicLine[line>>2][line&0x3];

        float ymin = 0.0;
        if (uOAM[fSpriteIndex].Rotscale != -1)
            ymin = -float(uOAM[fSpriteIndex].Size.y) / 2.0;

        float mosy = coord.y - float(line - mosline);
        if (coord.y >= ymin)
            coord.y = max(mosy, ymin);
    }

    if (uOAM[fSpriteIndex].Rotscale != -1)
    {
        // rotscale sprite
        // fTexcoord is based on the sprite center

        vec2 sprsize = vec2(uOAM[fSpriteIndex].Size);
        vec4 rotscale = vec4(uRotscale[uOAM[fSpriteIndex].Rotscale]) / 256.0;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        coord = (coord * rsmatrix) + (sprsize / 2.0);
        if (any(lessThan(coord, vec2(0)))) discard;
        if (any(greaterThanEqual(coord, sprsize))) discard;
    }

    if (uRenderTransparent)
    {
        // set BG priority and mosaic flags for transparent pixels

        if (uOAM[fSpriteIndex].Mosaic)
            flags.g = 1.0;

        flags.a = float(uOAM[fSpriteIndex].BGPrio) / 255.0;

        oColor = vec4(0);
        oFlags = flags;
        return;
    }

    if (uOAM[fSpriteIndex].Type == 3)
    {
        coord += vec2(ivec2(uOAM[fSpriteIndex].TileOffset) >> ivec2(1, 8));
        coord *= (1.0/128.0);
        col = texture(Capture256Tex, vec3(fract(coord), uOAM[fSpriteIndex].TileStride));
    }
    else if (uOAM[fSpriteIndex].Type == 4)
    {
        coord += vec2(ivec2(uOAM[fSpriteIndex].TileOffset) >> ivec2(1, 9));
        coord *= (1.0/256.0);
        col = texture(Capture256Tex, vec3(fract(coord), uOAM[fSpriteIndex].TileStride));
    }
    else
    {
        col = GetSpritePixel(fSpriteIndex, coord);
    }

    if (col.a == 0.0) discard;

    // oFlags:
    // r = sprite blending flag
    // g = mosaic flag
    // b = OBJ window flag
    // a = BG prio

    if (uOAM[fSpriteIndex].OBJMode == 2)
    {
        // OBJ window
        // OBJ mosaic doesn't apply to "OBJ window" sprites
        flags.b = 1.0;
    }
    else
    {
        if (uOAM[fSpriteIndex].OBJMode == 1)
        {
            // semi-transparent sprite
            flags.r = 1.0 / 255.0;
        }
        else if (uOAM[fSpriteIndex].OBJMode == 3)
        {
            // bitmap sprite
            col.a = float(uOAM[fSpriteIndex].PalOffset) / 31.0;
            flags.r = 2.0 / 255.0;
        }

        if (uOAM[fSpriteIndex].Mosaic)
            flags.g = 1.0;

        flags.a = float(uOAM[fSpriteIndex].BGPrio) / 255.0;
    }

    oColor = col;
    oFlags = flags;
}
