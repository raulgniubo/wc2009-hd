package io.github.wc2009hd;

import android.content.Context;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** The emulator: runs on the GLSurfaceView's thread, paced to the DS's 59.83 frames per second. */
final class GameView extends GLSurfaceView implements GLSurfaceView.Renderer {
    interface Host {
        /** GL thread, once: load the game. Returns an error message or null. */
        String startGame();
        void onStartFailed(String message);
    }

    private static final double DS_FPS = 59.8261;

    private final Host host;
    private final Layout layout;
    private final Input input;
    private boolean started, failed;
    private int width, height;
    private long baseNs;
    private long framesRun;
    private volatile boolean paused, fastForward;
    private volatile int ffSpeed = 3;

    GameView(Context c, Host host, Layout layout, Input input) {
        super(c);
        this.host = host; this.layout = layout; this.input = input;
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        setPreserveEGLContextOnPause(true);
        setRenderer(this);
        setRenderMode(RENDERMODE_CONTINUOUSLY);
    }

    void setPaused(boolean p) { paused = p; resync(); }
    void setFastForward(boolean f) { fastForward = f; resync(); }
    void setFastForwardSpeed(int s) { ffSpeed = Math.max(2, Math.min(4, s)); }
    private void resync() { baseNs = 0; }

    @Override
    public void onSurfaceCreated(GL10 unused, EGLConfig config) {
        if (failed) return;
        if (!started) {
            if (Native.everLoaded) Native.glReset();   // new view, same process: drop the old context's objects
            String err = host.startGame();
            if (err != null) { failed = true; host.onStartFailed(err); return; }
            started = true;
        } else {
            // the context was lost while the app was in the background
            Native.glReset();
        }
        resync();
    }

    @Override
    public void onSurfaceChanged(GL10 unused, int w, int h) {
        width = w; height = h;
    }

    @Override
    public void onDrawFrame(GL10 unused) {
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0);
        GLES30.glClearColor(0, 0, 0, 1);
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT);
        if (!started) return;

        if (!paused) {
            long now = System.nanoTime();
            int run;
            if (fastForward) {
                run = ffSpeed;
            } else {
                if (baseNs == 0) { baseNs = now; framesRun = 0; }
                long due = (long) ((now - baseNs) * DS_FPS / 1e9) + 1 - framesRun;
                if (due > 4) { baseNs = now; framesRun = 0; due = 1; }   // fell behind: don't race to catch up
                run = (int) Math.max(0, due);
                // keep the audio queue around 40-90 ms: one frame more or less when it drifts
                int q = Native.audioQueued();
                if (run == 0 && q < 48000 * 30 / 1000) run = 1;
                else if (run == 2 && q > 48000 * 90 / 1000) run = 1;
                framesRun += run;
            }
            for (int i = 0; i < run; i++) {
                input.apply();
                Native.runFrame();
            }
        }

        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0);
        draw(0, layout.top);
        draw(1, layout.bottom);
    }

    private void draw(int screen, android.graphics.RectF r) {
        Native.drawScreen(screen, Math.round(r.left), Math.round(r.top), Math.round(r.width()), Math.round(r.height()),
                width, height);
    }
}
