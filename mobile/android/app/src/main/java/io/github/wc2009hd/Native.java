package io.github.wc2009hd;

/** The WC2009 HD core (mobile/core/wc09_core.h). GL calls must run on the GL thread. */
final class Native {
    static { System.loadLibrary("wc09jni"); }

    // DS buttons (bit set = pressed)
    static final int A = 1, B = 1 << 1, SELECT = 1 << 2, START = 1 << 3, RIGHT = 1 << 4, LEFT = 1 << 5,
            UP = 1 << 6, DOWN = 1 << 7, R = 1 << 8, L = 1 << 9, X = 1 << 10, Y = 1 << 11;

    static native int init(String gameDir, String dataDir, String fontDir);
    static native String findRom();
    static native int loadRom(String path, int scale, boolean jit);
    static native void setScale(int scale);
    static native void runFrame();
    static native void drawScreen(int screen, int x, int y, int w, int h, int fbW, int fbH);
    static native void setKeys(int keys);
    static native void touch(boolean down, int x, int y);
    static native int saveState(int slot);
    static native int loadState(int slot);
    static native boolean stateExists(int slot);
    static native int deleteState(int slot);
    static native void flushSave();
    /** a game was loaded before in this process (its GL objects belong to an older context) */
    static boolean everLoaded;
    static native void glReset();
    static native int audioQueued();
    static native void audioStart();
    static native void audioStop();

    private Native() {}
}
