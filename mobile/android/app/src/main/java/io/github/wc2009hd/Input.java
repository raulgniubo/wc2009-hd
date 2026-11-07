package io.github.wc2009hd;

/**
 * Input shared between the UI thread (touch controls, gamepads) and the GL thread (emulator).
 * Short presses are held for a couple of frames so a tap between two frames isn't lost.
 */
final class Input {
    private static final int HOLD_FRAMES = 2;

    private int screenKeys, padKeys;           // held right now
    private int latchedKeys, latchFrames;      // recently pressed
    private boolean touchDown, touchLatched;
    private int touchX, touchY, touchFrames;
    private boolean lastTouchSent;

    synchronized void setScreenKeys(int k) { latch(k & ~screenKeys); screenKeys = k; }
    synchronized void setPadKeys(int k) { latch(k & ~padKeys); padKeys = k; }

    private void latch(int pressed) {
        if (pressed != 0) { latchedKeys |= pressed; latchFrames = HOLD_FRAMES; }
    }

    synchronized void touch(boolean down, int x, int y) {
        if (down) {
            if (!touchDown) { touchLatched = true; touchFrames = HOLD_FRAMES; }
            touchX = x; touchY = y;
        }
        touchDown = down;
    }

    /** GL thread, before each emulated frame. */
    synchronized void apply() {
        Native.setKeys(screenKeys | padKeys | latchedKeys);
        if (latchFrames > 0 && --latchFrames == 0) latchedKeys = 0;

        boolean down = touchDown || touchLatched;
        if (down) Native.touch(true, touchX, touchY);
        else if (lastTouchSent) Native.touch(false, 0, 0);
        lastTouchSent = down;
        if (touchFrames > 0 && --touchFrames == 0) touchLatched = false;
    }

    synchronized void releaseAll() {
        screenKeys = padKeys = latchedKeys = 0;
        touchDown = touchLatched = false;
    }
}
