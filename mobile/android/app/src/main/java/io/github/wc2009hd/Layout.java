package io.github.wc2009hd;

import android.graphics.RectF;

/**
 * Where the two DS screens and the touch controls go (pixels). Same rules as the iOS app.
 *
 *  - Tall phone, portrait:     screens stacked at the top, controls underneath.
 *  - Squarer screen, portrait  (foldables like iPhone Duo / Galaxy Fold opened, tablets):
 *                              "DS mode", top screen on the top half, bottom screen on the
 *                              bottom half (split at the fold), controls beside the bottom screen.
 *  - Wide phone, landscape:    screens side by side, controls in the side margins.
 *  - Squarer screen, landscape: screens side by side at the top, controls underneath.
 *  - Full screen (no buttons): screens as big as they fit; only a small menu button in a corner.
 */
final class Layout {
    final RectF top = new RectF(), bottom = new RectF();
    float dpadX, dpadY, dpadR;
    float abxyX, abxyY, abxyR, btnR;
    final RectF l = new RectF(), r = new RectF(), start = new RectF(), select = new RectF();
    final RectF menu = new RectF(), ffwd = new RectF();
    boolean landscape, fullscreen;

    private static final float TALL = 1.75f;   // height/width above this = a normal phone

    void compute(int w, int h, float u, boolean controls) {
        landscape = w > h;
        fullscreen = !controls;
        float W = w, H = h;
        if (fullscreen) full(W, H, u);
        else if (!landscape) { if (H / W >= TALL) phonePortrait(W, H, u); else foldPortrait(W, H, u); }
        else { if (W / H >= TALL || !wideLandscape(W, H, u)) phoneLandscape(W, H, u); }
    }

    // ---- screens

    private void full(float W, float H, float u) {
        if (!landscape) {
            if (H / W < TALL) {
                // one DS screen per half, meeting at the fold
                float half = H / 2, sh = Math.min(half, W * 0.75f), sw = sh / 0.75f;
                top.set((W - sw) / 2, half - sh, (W + sw) / 2, half);
                bottom.set((W - sw) / 2, half, (W + sw) / 2, half + sh);
            } else {
                float sw = Math.min(W, H / 1.5f), sh = sw * 0.75f, y0 = (H - 2 * sh) / 2;
                top.set((W - sw) / 2, y0, (W + sw) / 2, y0 + sh);
                bottom.set((W - sw) / 2, y0 + sh, (W + sw) / 2, y0 + 2 * sh);
            }
        } else {
            float sw = Math.min(W / 2, H / 0.75f), sh = sw * 0.75f, y0 = (H - sh) / 2;
            top.set(W / 2 - sw, y0, W / 2, y0 + sh);
            bottom.set(W / 2, y0, W / 2 + sw, y0 + sh);
        }
        float mb = 34 * u, pad = 16 * u;   // rounded display corners: keep a little clear
        menu.set(W - pad - mb, pad, W - pad, pad + mb);
        ffwd.set(-1, -1, -1, -1);
        dpadR = 0;
    }

    private void phonePortrait(float W, float H, float u) {
        float minCtrl = 230 * u;
        float sw = W, sh = sw * 0.75f;
        if (sh * 2 > H - minCtrl) { sh = Math.max((H - minCtrl) / 2, H * 0.3f); sw = sh / 0.75f; }
        float x0 = (W - sw) / 2;
        top.set(x0, 0, x0 + sw, sh);
        bottom.set(x0, sh, x0 + sw, 2 * sh);
        areaControls(0, Math.min(bottom.bottom, H - minCtrl), W, H, u);
    }

    private void foldPortrait(float W, float H, float u) {
        float half = H / 2;
        float sh = Math.min(half, W * 0.75f), sw = sh / 0.75f;
        top.set((W - sw) / 2, half - sh, (W + sw) / 2, half);
        float m = Math.max(110 * u, Math.min(170 * u, W * 0.2f));
        float bw = Math.min(W - 2 * m, half / 0.75f), bh = bw * 0.75f;
        bottom.set((W - bw) / 2, half, (W + bw) / 2, half + bh);
        marginControls(half, H, 0, m, W - m, W, u);
    }

    private void phoneLandscape(float W, float H, float u) {
        float m = Math.min(150 * u, W * 0.17f);
        float sw = Math.min((W - 2 * m) / 2, H / 0.75f), sh = sw * 0.75f, y0 = (H - sh) / 2;
        top.set(W / 2 - sw, y0, W / 2, y0 + sh);
        bottom.set(W / 2, y0, W / 2 + sw, y0 + sh);
        float mm = Math.max(m, 120 * u);   // controls overlap the screens a little on narrow phones
        marginControls(0, H, 0, mm, W - mm, W, u);
    }

    private boolean wideLandscape(float W, float H, float u) {
        float minCtrl = 170 * u;
        float sw = Math.min(W / 2, (H - minCtrl) / 0.75f), sh = sw * 0.75f;
        if (H - sh < 150 * u) return false;
        top.set(W / 2 - sw, 0, W / 2, sh);
        bottom.set(W / 2, 0, W / 2 + sw, sh);
        areaControls(0, sh, W, H, u);
        return true;
    }

    // ---- controls

    /** Controls in a box under the screens: d-pad left, ABXY right, L/R on top, START/SELECT below. */
    private void areaControls(float x0, float y0, float x1, float y1, float u) {
        float aw = x1 - x0, ah = y1 - y0;
        float side = Math.min(aw, ah * 2.2f), pad = 18 * u;
        dpadR = Math.min(72 * u, Math.min(ah * 0.36f, side * 0.2f));
        btnR = dpadR * 0.36f;
        abxyR = dpadR * 0.88f;
        float cy = y0 + ah * 0.52f;
        dpadX = x0 + pad + dpadR; dpadY = cy;
        abxyX = x1 - pad - abxyR - btnR; abxyY = cy;
        float lrW = 86 * u, lrH = 34 * u, lrY = y0 + 10 * u;
        l.set(x0 + pad, lrY, x0 + pad + lrW, lrY + lrH);
        r.set(x1 - pad - lrW, lrY, x1 - pad, lrY + lrH);
        float pillW = 64 * u, pillH = 26 * u, py = y1 - pad - pillH, cx = (x0 + x1) / 2;
        select.set(cx - 8 * u - pillW, py, cx - 8 * u, py + pillH);
        start.set(cx + 8 * u, py, cx + 8 * u + pillW, py + pillH);
        float mb = 40 * u, my = y0 + 10 * u;
        menu.set(cx + 6 * u, my, cx + 6 * u + mb, my + mb);
        ffwd.set(cx - 6 * u - mb, my, cx - 6 * u, my + mb);
    }

    /** Controls in two side columns: L, fast forward, d-pad, SELECT left; R, menu, ABXY, START right. */
    private void marginControls(float y0, float y1, float lx0, float lx1, float rx0, float rx1, float u) {
        float m = lx1 - lx0, pad = 14 * u, ah = y1 - y0;
        dpadR = Math.min(66 * u, Math.min((m - pad) * 0.5f, ah * 0.22f));
        btnR = dpadR * 0.36f;
        abxyR = dpadR * 0.88f;
        float cy = y0 + ah * 0.55f;
        dpadX = lx0 + pad + dpadR + 4 * u; dpadY = cy;
        abxyX = rx1 - pad - abxyR - btnR; abxyY = cy;
        float lrW = Math.min(86 * u, m - 2 * pad), lrH = 34 * u, t = y0 + pad;
        l.set(lx0 + pad, t, lx0 + pad + lrW, t + lrH);
        r.set(rx1 - pad - lrW, t, rx1 - pad, t + lrH);
        float pillW = Math.min(64 * u, m - 2 * pad), pillH = 26 * u, b = y1 - pad - pillH;
        select.set(lx0 + pad, b, lx0 + pad + pillW, b + pillH);
        start.set(rx1 - pad - pillW, b, rx1 - pad, b + pillH);
        float mb = 36 * u, my = l.bottom + 12 * u;
        menu.set(rx1 - pad - mb, my, rx1 - pad, my + mb);
        ffwd.set(lx0 + pad, my, lx0 + pad + mb, my + mb);
    }

    /** Moves everything by (dx, dy) (iOS: the safe area). */
    void offset(float dx, float dy) {
        for (RectF rc : new RectF[] {top, bottom, l, r, start, select, menu, ffwd}) rc.offset(dx, dy);
        dpadX += dx; dpadY += dy; abxyX += dx; abxyY += dy;
    }
}
