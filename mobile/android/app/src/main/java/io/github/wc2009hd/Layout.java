package io.github.wc2009hd;

import android.graphics.RectF;

/**
 * Where the two DS screens and the touch controls go, for a view of w x h pixels.
 * Portrait: screens stacked at the top, controls underneath.
 * Landscape: screens side by side (top screen left), controls in the side margins.
 */
final class Layout {
    final RectF top = new RectF(), bottom = new RectF();
    // control centres / boxes (pixels)
    float dpadX, dpadY, dpadR;
    float abxyX, abxyY, abxyR, btnR;
    final RectF l = new RectF(), r = new RectF(), start = new RectF(), select = new RectF();
    final RectF menu = new RectF(), ffwd = new RectF();
    boolean landscape;

    void compute(int w, int h, float dp, boolean controls) {
        landscape = w > h;
        if (!landscape) portrait(w, h, dp, controls);
        else land(w, h, dp, controls);
    }

    private void portrait(int w, int h, float dp, boolean controls) {
        float minCtrl = controls ? 230 * dp : 0;
        float sw = w, sh = sw * 0.75f;
        if (sh * 2 > h - minCtrl) { sh = Math.max((h - minCtrl) / 2, h * 0.3f); sw = sh / 0.75f; }
        float x0 = (w - sw) / 2, gap = 0;
        top.set(x0, 0, x0 + sw, sh);
        bottom.set(x0, sh + gap, x0 + sw, 2 * sh + gap);

        // controls area: below the screens (overlapping the bottom screen if there isn't room)
        float areaTop = Math.min(bottom.bottom, h - minCtrl);
        float areaH = h - areaTop;
        float side = Math.min(w, areaH * 2.2f);
        float pad = 18 * dp;
        dpadR = Math.min(72 * dp, Math.min(areaH * 0.36f, side * 0.2f));
        btnR = dpadR * 0.36f;
        abxyR = dpadR * 0.88f;
        float cy = areaTop + areaH * 0.52f;
        dpadX = pad + dpadR; dpadY = cy;
        abxyX = w - pad - abxyR - btnR; abxyY = cy;

        float lrW = 86 * dp, lrH = 34 * dp, lrY = areaTop + 10 * dp;
        l.set(pad, lrY, pad + lrW, lrY + lrH);
        r.set(w - pad - lrW, lrY, w - pad, lrY + lrH);

        float pillW = 64 * dp, pillH = 26 * dp, py = h - pad - pillH;
        select.set(w / 2f - 8 * dp - pillW, py, w / 2f - 8 * dp, py + pillH);
        start.set(w / 2f + 8 * dp, py, w / 2f + 8 * dp + pillW, py + pillH);

        float mb = 40 * dp, my = areaTop + 10 * dp;
        menu.set(w / 2f + 6 * dp, my, w / 2f + 6 * dp + mb, my + mb);
        ffwd.set(w / 2f - 6 * dp - mb, my, w / 2f - 6 * dp, my + mb);
    }

    private void land(int w, int h, float dp, boolean controls) {
        float margin = controls ? Math.min(150 * dp, w * 0.17f) : 0;
        float avail = w - 2 * margin;
        float sw = Math.min(avail / 2, h / 0.75f), sh = sw * 0.75f;
        float cx = w / 2f, y0 = (h - sh) / 2;
        top.set(cx - sw, y0, cx, y0 + sh);
        bottom.set(cx, y0, cx + sw, y0 + sh);

        float m = Math.max(margin, 120 * dp);   // controls overlap the screens a little on narrow phones
        float pad = 14 * dp;
        dpadR = Math.min(66 * dp, (m - pad) * 0.5f);
        btnR = dpadR * 0.36f;
        abxyR = dpadR * 0.88f;
        dpadX = pad + dpadR + 4 * dp; dpadY = h * 0.55f;
        abxyX = w - pad - abxyR - btnR; abxyY = h * 0.55f;

        float lrW = Math.min(86 * dp, m - 2 * pad), lrH = 34 * dp;
        l.set(pad, pad, pad + lrW, pad + lrH);
        r.set(w - pad - lrW, pad, w - pad, pad + lrH);

        float pillW = Math.min(64 * dp, m - 2 * pad), pillH = 26 * dp;
        select.set(pad, h - pad - pillH, pad + pillW, h - pad);
        start.set(w - pad - pillW, h - pad - pillH, w - pad, h - pad);

        float mb = 36 * dp;
        menu.set(w - pad - mb, l.bottom + 12 * dp, w - pad, l.bottom + 12 * dp + mb);
        ffwd.set(pad, l.bottom + 12 * dp, pad + mb, l.bottom + 12 * dp + mb);
        if (!controls) {
            menu.set(w - pad - mb, pad, w - pad, pad + mb);
        }
    }
}
