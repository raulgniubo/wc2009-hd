package io.github.wc2009hd;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

/** On-screen DS buttons + the bottom screen's touch area. Draws over the GL view. */
final class ControlsView extends View {
    interface Listener { void onMenu(); void onFastForward(boolean on); }

    private final Layout layout;
    private final Input input;
    private final Listener listener;
    private final float dp;
    private boolean showButtons = true;
    private float opacity = 0.6f;
    private boolean ffwdOn;
    private boolean haptics = true;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    // pointer id -> what it holds: key mask, or -1 for the touch screen
    private final android.util.SparseIntArray held = new android.util.SparseIntArray();
    private int touchPointer = -1;
    private int lastKeys;

    ControlsView(Context c, Layout layout, Input input, Listener listener) {
        super(c);
        this.layout = layout; this.input = input; this.listener = listener;
        dp = c.getResources().getDisplayMetrics().density;
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2 * dp);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
    }

    void setShowButtons(boolean on) { showButtons = on; invalidate(); }
    void setOpacity(float o) { opacity = o; invalidate(); }
    void setFastForward(boolean on) { ffwdOn = on; invalidate(); }
    void setHaptics(boolean on) { haptics = on; }

    // ---- input ----

    private int keysAt(float x, float y) {
        if (!showButtons) return 0;
        // d-pad: direction from the centre, 8 ways
        float dx = x - layout.dpadX, dy = y - layout.dpadY;
        float dist = (float) Math.hypot(dx, dy);
        if (dist < layout.dpadR * 1.25f && dist > layout.dpadR * 0.15f) {
            double a = Math.toDegrees(Math.atan2(dy, dx));   // 0 = right, 90 = down
            int k = 0;
            if (a > -67.5 && a < 67.5) k |= Native.RIGHT;
            if (a > 112.5 || a < -112.5) k |= Native.LEFT;
            if (a > 22.5 && a < 157.5) k |= Native.DOWN;
            if (a < -22.5 && a > -157.5) k |= Native.UP;
            return k;
        }
        // face buttons: nearest one (pressing between two presses both)
        float r = layout.btnR * 1.35f;
        int k = 0;
        float[][] b = faceButtons();
        int[] bits = {Native.A, Native.B, Native.X, Native.Y};
        for (int i = 0; i < 4; i++)
            if (Math.hypot(x - b[i][0], y - b[i][1]) < r) k |= bits[i];
        if (k != 0) return k;
        if (grow(layout.l).contains(x, y)) return Native.L;
        if (grow(layout.r).contains(x, y)) return Native.R;
        if (grow(layout.start).contains(x, y)) return Native.START;
        if (grow(layout.select).contains(x, y)) return Native.SELECT;
        return 0;
    }

    private final RectF tmp = new RectF();
    private RectF grow(RectF rc) { tmp.set(rc); tmp.inset(-8 * dp, -8 * dp); return tmp; }

    private float[][] faceButtons() {
        float cx = layout.abxyX, cy = layout.abxyY, d = layout.abxyR;
        return new float[][] {{cx + d, cy}, {cx, cy + d}, {cx, cy - d}, {cx - d, cy}};   // A B X Y
    }

    private boolean inBottom(float x, float y) { return layout.bottom.contains(x, y); }

    private void sendTouch(float x, float y, boolean down) {
        RectF b = layout.bottom;
        int tx = (int) ((x - b.left) * 256f / b.width());
        int ty = (int) ((y - b.top) * 192f / b.height());
        input.touch(down, Math.max(0, Math.min(255, tx)), Math.max(0, Math.min(191, ty)));
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int idx = e.getActionIndex();
        int id = e.getPointerId(idx);
        float x = e.getX(idx), y = e.getY(idx);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (layout.menu.contains(x, y)) { listener.onMenu(); return true; }
                if (showButtons && grow(layout.ffwd).contains(x, y)) {
                    ffwdOn = !ffwdOn; listener.onFastForward(ffwdOn); invalidate(); return true;
                }
                int k = keysAt(x, y);
                if (k != 0) held.put(id, k);
                else if (inBottom(x, y) && touchPointer < 0) {
                    touchPointer = id; held.put(id, -1);
                    sendTouch(x, y, true);
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int pid = e.getPointerId(i);
                    if (held.indexOfKey(pid) < 0) continue;
                    float px = e.getX(i), py = e.getY(i);
                    if (pid == touchPointer) sendTouch(px, py, true);
                    else {
                        // sliding between buttons (d-pad directions, A to B...) follows the finger
                        int k = keysAt(px, py);
                        held.put(pid, k);
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (id == touchPointer) { sendTouch(x, y, false); touchPointer = -1; }
                held.delete(id);
                break;
            case MotionEvent.ACTION_CANCEL:
                held.clear();
                if (touchPointer >= 0) { input.touch(false, 0, 0); touchPointer = -1; }
                break;
        }
        updateKeys();
        return true;
    }

    private void updateKeys() {
        int k = 0;
        for (int i = 0; i < held.size(); i++) if (held.valueAt(i) > 0) k |= held.valueAt(i);
        if (haptics && (k & ~lastKeys) != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        if (k != lastKeys) { lastKeys = k; input.setScreenKeys(k); invalidate(); }
    }

    // ---- drawing ----

    @Override
    protected void onDraw(Canvas c) {
        int a = (int) (255 * opacity);
        drawMenuButton(c, a);
        if (!showButtons) return;
        drawDpad(c, a);
        float[][] b = faceButtons();
        String[] names = {"A", "B", "X", "Y"};
        int[] bits = {Native.A, Native.B, Native.X, Native.Y};
        for (int i = 0; i < 4; i++) drawRound(c, b[i][0], b[i][1], layout.btnR, names[i], (lastKeys & bits[i]) != 0, a);
        drawPill(c, layout.l, "L", (lastKeys & Native.L) != 0, a, layout.btnR * 0.75f);
        drawPill(c, layout.r, "R", (lastKeys & Native.R) != 0, a, layout.btnR * 0.75f);
        drawPill(c, layout.select, "SELECT", (lastKeys & Native.SELECT) != 0, a, 11 * dp);
        drawPill(c, layout.start, "START", (lastKeys & Native.START) != 0, a, 11 * dp);
        drawFfwd(c, a);
    }

    private void setFill(boolean pressed, int a) {
        fill.setColor(pressed ? 0xFFFFFFFF : 0xFF20242C);
        fill.setAlpha(pressed ? Math.min(255, a + 40) : (int) (a * 0.75f));
        stroke.setColor(0xFFFFFFFF);
        stroke.setAlpha((int) (a * 0.8f));
        text.setColor(pressed ? 0xFF101318 : 0xFFFFFFFF);
        text.setAlpha(Math.min(255, a + 60));
    }

    private void drawRound(Canvas c, float x, float y, float r, String label, boolean pressed, int a) {
        setFill(pressed, a);
        c.drawCircle(x, y, r, fill);
        c.drawCircle(x, y, r, stroke);
        text.setTextSize(r * 0.95f);
        c.drawText(label, x, y - (text.descent() + text.ascent()) / 2, text);
    }

    private void drawPill(Canvas c, RectF rc, String label, boolean pressed, int a, float size) {
        setFill(pressed, a);
        float rr = rc.height() / 2;
        c.drawRoundRect(rc, rr, rr, fill);
        c.drawRoundRect(rc, rr, rr, stroke);
        text.setTextSize(size);
        c.drawText(label, rc.centerX(), rc.centerY() - (text.descent() + text.ascent()) / 2, text);
    }

    private void drawDpad(Canvas c, int a) {
        float x = layout.dpadX, y = layout.dpadY, r = layout.dpadR, t = r * 0.36f;
        setFill(false, a);
        path.reset();
        path.addRoundRect(new RectF(x - t, y - r, x + t, y + r), 6 * dp, 6 * dp, Path.Direction.CW);
        path.addRoundRect(new RectF(x - r, y - t, x + r, y + t), 6 * dp, 6 * dp, Path.Direction.CW);
        path.setFillType(Path.FillType.WINDING);
        c.drawPath(path, fill);
        // pressed directions
        Paint hp = new Paint(fill);
        hp.setColor(0xFFFFFFFF); hp.setAlpha(Math.min(255, a + 40));
        if ((lastKeys & Native.UP) != 0) c.drawRoundRect(new RectF(x - t, y - r, x + t, y - t), 6 * dp, 6 * dp, hp);
        if ((lastKeys & Native.DOWN) != 0) c.drawRoundRect(new RectF(x - t, y + t, x + t, y + r), 6 * dp, 6 * dp, hp);
        if ((lastKeys & Native.LEFT) != 0) c.drawRoundRect(new RectF(x - r, y - t, x - t, y + t), 6 * dp, 6 * dp, hp);
        if ((lastKeys & Native.RIGHT) != 0) c.drawRoundRect(new RectF(x + t, y - t, x + r, y + t), 6 * dp, 6 * dp, hp);
        // outline (union of the two bars)
        Path o = new Path();
        o.moveTo(x - t, y - r); o.lineTo(x + t, y - r); o.lineTo(x + t, y - t); o.lineTo(x + r, y - t);
        o.lineTo(x + r, y + t); o.lineTo(x + t, y + t); o.lineTo(x + t, y + r); o.lineTo(x - t, y + r);
        o.lineTo(x - t, y + t); o.lineTo(x - r, y + t); o.lineTo(x - r, y - t); o.lineTo(x - t, y - t); o.close();
        c.drawPath(o, stroke);
        // arrows
        text.setColor(0xFFFFFFFF); text.setAlpha(Math.min(255, a + 40));
        float h = t * 0.55f, d = r - t * 0.55f;
        triangle(c, x, y - d, 0, -1, h); triangle(c, x, y + d, 0, 1, h);
        triangle(c, x - d, y, -1, 0, h); triangle(c, x + d, y, 1, 0, h);
    }

    private void triangle(Canvas c, float x, float y, float dx, float dy, float s) {
        Path p = new Path();
        p.moveTo(x + dx * s * 0.6f, y + dy * s * 0.6f);
        p.lineTo(x - dy * s * 0.6f - dx * s * 0.4f, y + dx * s * 0.6f - dy * s * 0.4f);
        p.lineTo(x + dy * s * 0.6f - dx * s * 0.4f, y - dx * s * 0.6f - dy * s * 0.4f);
        p.close();
        c.drawPath(p, text);
    }

    private void drawMenuButton(Canvas c, int a) {
        RectF m = layout.menu;
        setFill(false, Math.max(a, 140));
        c.drawRoundRect(m, 10 * dp, 10 * dp, fill);
        stroke.setAlpha(Math.max(a, 140));
        c.drawRoundRect(m, 10 * dp, 10 * dp, stroke);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(0xFFFFFFFF); p.setAlpha(230); p.setStrokeWidth(2.5f * dp); p.setStrokeCap(Paint.Cap.ROUND);
        float w = m.width() * 0.26f;
        for (int i = -1; i <= 1; i++) {
            float y = m.centerY() + i * m.height() * 0.2f;
            c.drawLine(m.centerX() - w, y, m.centerX() + w, y, p);
        }
    }

    private void drawFfwd(Canvas c, int a) {
        RectF m = layout.ffwd;
        setFill(ffwdOn, a);
        c.drawRoundRect(m, 10 * dp, 10 * dp, fill);
        c.drawRoundRect(m, 10 * dp, 10 * dp, stroke);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(ffwdOn ? 0xFF101318 : 0xFFFFFFFF); p.setAlpha(230);
        float s = m.height() * 0.2f, cx = m.centerX(), cy = m.centerY();
        for (int i = 0; i < 2; i++) {
            float ox = cx - s * 0.9f + i * s * 1.0f;
            Path t = new Path();
            t.moveTo(ox, cy - s); t.lineTo(ox + s, cy); t.lineTo(ox, cy + s); t.close();
            c.drawPath(t, p);
        }
    }
}
