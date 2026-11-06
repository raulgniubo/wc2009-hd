#version 140

uniform sampler2D BGLayerTex[4];
uniform sampler2DArray OBJLayerTex;
uniform sampler2DArray Capture128Tex;
uniform sampler2DArray Capture256Tex;
uniform isampler2D MosaicTex;

struct sBGConfig
{
    ivec2 Size;
    int Type;
    int PalOffset;
    int TileOffset;
    int MapOffset;
    bool Clamp;
};

layout(std140) uniform ubBGConfig
{
    int uVRAMMask;
    sBGConfig uBGConfig[4];
};

struct sScanline
{
    ivec2 BGOffset[4];
    ivec4 BGRotscale[2];
    int BackColor;
    uint WinRegs;
    int WinMask;
    ivec4 WinPos;
    bvec4 BGMosaicEnable;
    ivec4 MosaicSize;
};

layout(std140) uniform ubScanlineConfig
{
    sScanline uScanline[192];
};

layout(std140) uniform ubCompositorConfig
{
    ivec4 uBGPrio;
    bool uEnableOBJ;
    bool uEnable3D;
    int uBlendCnt;
    int uBlendEffect;
    ivec3 uBlendCoef;
};

uniform int uScaleFactor;

smooth in vec4 fTexcoord;

out vec4 oColor;

int MosaicX = 0;

// ---- WC09: xBR-lv2 style edge-directed upscaling (after Hyllian's xBR, MIT) ----
uniform bool uSmooth2D;

// WC09 HD background images (battle monsters): sampled instead of the original pixels
uniform sampler2D HDTex[4];
uniform bool uHDOn[4];
uniform vec4 uHDRect[4];     // origin x, y and size w, h in BG pixels
uniform bool uHDMirror[4];
uniform bool uHDScreen[4];   // rect is in screen space (text panels)
uniform bool uHDFallback[4]; // transparent HD pixels fall back to the original (card sets)

vec4 HDScreenFetch(int n, vec2 coord, int line, out bool hit)
{
    hit = false;
    vec2 sp = vec2(coord.x, float(line) + coord.y);
    vec2 uv = (sp - uHDRect[n].xy) / uHDRect[n].zw;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return vec4(0.0);
    hit = true;
    vec4 h;
    if (n == 0) h = texture(HDTex[0], uv);
    else if (n == 1) h = texture(HDTex[1], uv);
    else if (n == 2) h = texture(HDTex[2], uv);
    else h = texture(HDTex[3], uv);
    return (h.a >= 0.5) ? vec4(h.rgb, 1.0) : vec4(0.0);
}

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

vec4 XbrTexelBG(sampler2D tex, ivec2 size, ivec2 p)
{
    p = ivec2(p.x & (size.x - 1), p.y & (size.y - 1));     // BG sizes are powers of two
    vec4 c = texelFetch(tex, p, 0);
    return vec4(c.rgb * c.a, c.a);                          // premultiplied
}

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

vec4 XbrFetch(sampler2D tex, ivec2 size, vec2 uv)
{
    vec2 pos = uv * vec2(size);
    vec2 fp = fract(pos);
    ivec2 p = ivec2(floor(pos));
#define T(dx, dy) XbrTexelBG(tex, size, p + ivec2(dx, dy))
    vec4 r = XbrResolve(T(-1,-2), T(0,-2), T(1,-2), T(-1,-1), T(0,-1), T(1,-1), T(-1,0), T(0,0), T(1,0),
                        T(-1,1), T(0,1), T(1,1), T(-1,2), T(0,2), T(1,2), T(-2,-1), T(-2,0), T(-2,1),
                        T(2,-1), T(2,0), T(2,1), fp, max(float(uScaleFactor), 1.0));
#undef T
    return r;
}

vec4 BG0Fetch(vec2 coord)
{
    if (uHDOn[0] && !uHDScreen[0])
    {
        vec2 bgsize = vec2(uBGConfig[0].Size);
        vec2 uv = mod(coord * bgsize - uHDRect[0].xy, bgsize) / uHDRect[0].zw;
        if (uv.x < 1.0 && uv.y < 1.0)
        {
            if (uHDMirror[0]) uv.x = 1.0 - uv.x;
            vec4 h = texture(HDTex[0], uv);
            if (h.a >= 0.5) return vec4(h.rgb, 1.0);
            if (!uHDFallback[0]) return vec4(0.0);
        }
    }
    if (uSmooth2D && uScaleFactor > 1 && uBGConfig[0].Type < 7 && !uEnable3D)   // BG0 may be the hi-res 3D layer
        return XbrFetch(BGLayerTex[0], uBGConfig[0].Size, coord);
    return texture(BGLayerTex[0], coord);
}

vec4 BG1Fetch(vec2 coord)
{
    if (uHDOn[1] && !uHDScreen[1])
    {
        vec2 bgsize = vec2(uBGConfig[1].Size);
        vec2 uv = mod(coord * bgsize - uHDRect[1].xy, bgsize) / uHDRect[1].zw;
        if (uv.x < 1.0 && uv.y < 1.0)
        {
            if (uHDMirror[1]) uv.x = 1.0 - uv.x;
            vec4 h = texture(HDTex[1], uv);
            if (h.a >= 0.5) return vec4(h.rgb, 1.0);
            if (!uHDFallback[1]) return vec4(0.0);
        }
    }
    if (uSmooth2D && uScaleFactor > 1 && uBGConfig[1].Type < 7)
        return XbrFetch(BGLayerTex[1], uBGConfig[1].Size, coord);
    return texture(BGLayerTex[1], coord);
}

vec4 BG2Fetch(vec2 coord)
{
    if (uHDOn[2] && !uHDScreen[2])
    {
        vec2 bgsize = vec2(uBGConfig[2].Size);
        vec2 uv = mod(coord * bgsize - uHDRect[2].xy, bgsize) / uHDRect[2].zw;
        if (uv.x < 1.0 && uv.y < 1.0)
        {
            if (uHDMirror[2]) uv.x = 1.0 - uv.x;
            vec4 h = texture(HDTex[2], uv);
            if (h.a >= 0.5) return vec4(h.rgb, 1.0);
            if (!uHDFallback[2]) return vec4(0.0);
        }
    }
    if (uSmooth2D && uScaleFactor > 1 && uBGConfig[2].Type < 7)
        return XbrFetch(BGLayerTex[2], uBGConfig[2].Size, coord);
    return texture(BGLayerTex[2], coord);
}

vec4 BG3Fetch(vec2 coord)
{
    if (uHDOn[3] && !uHDScreen[3])
    {
        vec2 bgsize = vec2(uBGConfig[3].Size);
        vec2 uv = mod(coord * bgsize - uHDRect[3].xy, bgsize) / uHDRect[3].zw;
        if (uv.x < 1.0 && uv.y < 1.0)
        {
            if (uHDMirror[3]) uv.x = 1.0 - uv.x;
            vec4 h = texture(HDTex[3], uv);
            if (h.a >= 0.5) return vec4(h.rgb, 1.0);
            if (!uHDFallback[3]) return vec4(0.0);
        }
    }
    if (uSmooth2D && uScaleFactor > 1 && uBGConfig[3].Type < 7)
        return XbrFetch(BGLayerTex[3], uBGConfig[3].Size, coord);
    return texture(BGLayerTex[3], coord);
}

vec4 BG0CalcAndFetch(vec2 coord, int line)
{
    if (uHDOn[0] && uHDScreen[0])
    {
        bool hit;
        vec4 h = HDScreenFetch(0, coord, line, hit);
        if (hit) return h;
    }
    ivec2 bgoffset = uScanline[line].BGOffset[0];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[0])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    return BG0Fetch(bgpos / vec2(uBGConfig[0].Size));
}

vec4 BG1CalcAndFetch(vec2 coord, int line)
{
    if (uHDOn[1] && uHDScreen[1])
    {
        bool hit;
        vec4 h = HDScreenFetch(1, coord, line, hit);
        if (hit) return h;
    }
    ivec2 bgoffset = uScanline[line].BGOffset[1];
    vec2 bgpos = vec2(bgoffset.xy) + coord;

    if (uScanline[line].BGMosaicEnable[1])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    return BG1Fetch(bgpos / vec2(uBGConfig[1].Size));
}

vec4 BG2CalcAndFetch(vec2 coord, int line)
{
    if (uHDOn[2] && uHDScreen[2])
    {
        bool hit;
        vec4 h = HDScreenFetch(2, coord, line, hit);
        if (hit) return h;
    }
    ivec2 bgoffset = uScanline[line].BGOffset[2];
    vec2 bgpos;
    if (uBGConfig[2].Type >= 2)
    {
        // rotscale BG
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[0]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        // text-mode BG
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[2])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    if (uBGConfig[2].Type >= 7)
    {
        // hi-res capture
        bgpos.y += uBGConfig[2].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[2].Size), uBGConfig[2].TileOffset);

        // due to the possible weirdness of display capture buffers,
        // we need to do custom wraparound handling
        if (uBGConfig[2].Clamp)
        {
            if (any(lessThan(capcoord.xy, vec2(0))) || any(greaterThanEqual(capcoord.xy, vec2(1))))
                return vec4(0);
        }

        if (uBGConfig[2].Type == 7)
            return texture(Capture128Tex, capcoord);
        else
            return texture(Capture256Tex, capcoord);
    }

    return BG2Fetch(bgpos / vec2(uBGConfig[2].Size));
}

vec4 BG3CalcAndFetch(vec2 coord, int line)
{
    if (uHDOn[3] && uHDScreen[3])
    {
        bool hit;
        vec4 h = HDScreenFetch(3, coord, line, hit);
        if (hit) return h;
    }
    ivec2 bgoffset = uScanline[line].BGOffset[3];
    vec2 bgpos;
    if (uBGConfig[3].Type >= 2)
    {
        // rotscale BG
        bgpos = vec2(bgoffset.xy) / 256;
        vec4 rotscale = vec4(uScanline[line].BGRotscale[1]) / 256;
        mat2 rsmatrix = mat2(rotscale.xy, rotscale.zw);
        bgpos = bgpos + (coord * rsmatrix);
    }
    else
    {
        // text-mode BG
        bgpos = vec2(bgoffset.xy) + coord;
    }

    if (uScanline[line].BGMosaicEnable[3])
    {
        bgpos = floor(bgpos) - vec2(MosaicX, 0);
    }

    if (uBGConfig[3].Type >= 7)
    {
        // hi-res capture
        bgpos.y += uBGConfig[3].MapOffset;
        vec3 capcoord = vec3(bgpos / vec2(uBGConfig[3].Size), uBGConfig[3].TileOffset);

        // due to the possible weirdness of display capture buffers,
        // we need to do custom wraparound handling
        if (uBGConfig[3].Clamp)
        {
            if (any(lessThan(capcoord.xy, vec2(0))) || any(greaterThanEqual(capcoord.xy, vec2(1))))
                return vec4(0);
        }

        if (uBGConfig[3].Type == 7)
            return texture(Capture128Tex, capcoord);
        else
            return texture(Capture256Tex, capcoord);
    }

    return BG3Fetch(bgpos / vec2(uBGConfig[3].Size));
}

void CalcSpriteMosaic(in ivec2 coord, out ivec4 objflags, out vec4 objcolor)
{
    for (int i = 0; i < 16; i++)
    {
        ivec2 curpos = ivec2(coord.x - 15 + i, coord.y);

        if (curpos.x < 0)
        {
            objflags = ivec4(0);
            objcolor = vec4(0);
        }
        else
        {
            int mosx = texelFetch(MosaicTex, ivec2(curpos.x, uScanline[curpos.y].MosaicSize.z), 0).r;
            vec4 color = texelFetch(OBJLayerTex, ivec3(curpos * uScaleFactor, 0), 0);
            ivec4 flags = ivec4(texelFetch(OBJLayerTex, ivec3(curpos * uScaleFactor, 1), 0) * 255.0);

            bool latch = false;
            if (mosx == 0)
                latch = true;
            else if (flags.g == 0)
                latch = true;
            else if (objflags.g == 0)
                latch = true;
            else if (flags.a < objflags.a)
                latch = true;

            if (latch)
            {
                objflags = flags;
                objcolor = color;
            }
        }
    }
}

vec4 CompositeLayers()
{
    ivec2 coord = ivec2(fTexcoord.zw);
    vec2 bgcoord = vec2(fTexcoord.x, fract(fTexcoord.y));
    int xpos = int(fTexcoord.x);
    int line = int(fTexcoord.y);

    if (uScanline[line].MosaicSize.x > 0)
        MosaicX = texelFetch(MosaicTex, ivec2(bgcoord.x, uScanline[line].MosaicSize.x), 0).r;

    ivec4 col1 = ivec4(ConvertColor(uScanline[line].BackColor), 0x20);
    int mask1 = 0x20;
    ivec4 col2 = ivec4(0);
    int mask2 = 0;
    bool specialcase = false;

    vec4 layercol[6];
    layercol[0] = BG0CalcAndFetch(bgcoord, line);
    layercol[1] = BG1CalcAndFetch(bgcoord, line);
    layercol[2] = BG2CalcAndFetch(bgcoord, line);
    layercol[3] = BG3CalcAndFetch(bgcoord, line);

    ivec4 objflags;
    if (uScanline[line].MosaicSize.z > 0)
    {
        CalcSpriteMosaic(ivec2(fTexcoord.xy), objflags, layercol[4]);
    }
    else
    {
        layercol[4] = texelFetch(OBJLayerTex, ivec3(coord, 0), 0);
        layercol[5] = texelFetch(OBJLayerTex, ivec3(coord, 1), 0);
        objflags = ivec4(layercol[5] * 255.0);
    }

    int winmask = uScanline[line].WinMask;
    bool inside_win0, inside_win1;

    if (xpos < uScanline[line].WinPos[0])
        inside_win0 = ((winmask & (1<<0)) != 0);
    else if (xpos < uScanline[line].WinPos[1])
        inside_win0 = ((winmask & (1<<1)) != 0);
    else
        inside_win0 = ((winmask & (1<<2)) != 0);

    if (xpos < uScanline[line].WinPos[2])
        inside_win1 = ((winmask & (1<<3)) != 0);
    else if (xpos < uScanline[line].WinPos[3])
        inside_win1 = ((winmask & (1<<4)) != 0);
    else
        inside_win1 = ((winmask & (1<<5)) != 0);

    uint winregs = uScanline[line].WinRegs;
    uint winsel = winregs;
    if (objflags.b > 0)
        winsel = winregs >> 8;
    if (inside_win1)
        winsel = winregs >> 16;
    if (inside_win0)
        winsel = winregs >> 24;

    for (int prio = 3; prio >= 0; prio--)
    {
        for (int bg = 3; bg >= 0; bg--)
        {
            if ((uBGPrio[bg] == prio) && (layercol[bg].a > 0) && ((winsel & (1u << bg)) != 0u))
            {
                col2 = col1;
                mask2 = mask1 << 8;
                col1 = ivec4(layercol[bg] * 255.0) >> ivec4(2,2,2,3);
                mask1 = (1 << bg);
                specialcase = (bg == 0) && uEnable3D;
            }
        }

        if (uEnableOBJ && (objflags.a == prio) && (layercol[4].a > 0) && ((winsel & (1u << 4)) != 0u))
        {
            col2 = col1;
            mask2 = mask1 << 8;
            col1 = ivec4(layercol[4] * 255.0) >> ivec4(2,2,2,3);
            mask1 = (1 << 4);
            specialcase = (objflags.r != 0);
        }
    }

    int effect = 0;
    int eva, evb, evy = uBlendCoef[2];

    if (specialcase && (uBlendCnt & mask2) != 0)
    {
        if (mask1 == (1<<0))
        {
            // 3D layer blending
            effect = 4;
            eva = (col1.a & 0x1F) + 1;
            evb = 32 - eva;
        }
        else if (objflags.r == 1)
        {
            // semi-transparent sprite
            effect = 1;
            eva = uBlendCoef[0];
            evb = uBlendCoef[1];
        }
        else //if (objflags.r == 2)
        {
            // bitmap sprite
            effect = 1;
            eva = col1.a;
            evb = 16 - eva;
        }
    }
    else if (((uBlendCnt & mask1) != 0) && ((winsel & (1u << 5)) != 0u))
    {
        effect = uBlendEffect;
        if (effect == 1)
        {
            if ((uBlendCnt & mask2) != 0)
            {
                eva = uBlendCoef[0];
                evb = uBlendCoef[1];
            }
            else
                effect = 0;
        }
    }

    if (effect == 1)
    {
        // blending
        col1 = ((col1 * eva) + (col2 * evb) + 0x8) >> 4;
        col1 = min(col1, 0x3F);
    }
    else if (effect == 2)
    {
        // brightness up
        col1 = col1 + ((((0x3F - col1) * evy) + 0x8) >> 4);
    }
    else if (effect == 3)
    {
        // brightness down
        col1 = col1 - (((col1 * evy) + 0x7) >> 4);
    }
    else if (effect == 4)
    {
        // 3D layer blending
        col1 = ((col1 * eva) + (col2 * evb) + 0x10) >> 5;
    }

    return vec4(vec3(col1.rgb << 2) / 255.0, 1);
}

void main()
{
    oColor = CompositeLayers();
}
