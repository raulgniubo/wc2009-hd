package io.github.wc2009hd;

import android.content.SharedPreferences;
import android.hardware.input.InputManager;
import android.view.InputDevice;
import android.view.KeyEvent;

/** Game controllers: which controller button does what in the game (remappable). */
final class Pad {
    // controller buttons (Android names them by position: A bottom, B right, X left, Y top)
    static final int A = 0, B = 1, X = 2, Y = 3, L1 = 4, R1 = 5, L2 = 6, R2 = 7, L3 = 8, R3 = 9, START = 10, SELECT = 11, HOME = 12, COUNT = 13;
    static final String[] BUTTON_NAMES = {"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "Left stick click", "Right stick click", "Start / Menu", "Select / View", "Home"};
    // what they can do
    static final int NONE = 0, DS_A = 1, DS_B = 2, DS_X = 3, DS_Y = 4, DS_L = 5, DS_R = 6, DS_START = 7, DS_SELECT = 8, MENU = 9, FFWD = 10, ACTIONS = 11;
    static final String[] ACTION_NAMES = {"Nothing", "A", "B", "X", "Y", "L", "R", "START", "SELECT", "Dual HD menu", "Fast forward (hold)"};
    static final int[] ACTION_KEYS = {0, Native.A, Native.B, Native.X, Native.Y, Native.L, Native.R, Native.START, Native.SELECT, 0, 0};

    static int fromKeyCode(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: return A;
            case KeyEvent.KEYCODE_BUTTON_B: return B;
            case KeyEvent.KEYCODE_BUTTON_X: return X;
            case KeyEvent.KEYCODE_BUTTON_Y: return Y;
            case KeyEvent.KEYCODE_BUTTON_L1: return L1;
            case KeyEvent.KEYCODE_BUTTON_R1: return R1;
            case KeyEvent.KEYCODE_BUTTON_L2: return L2;
            case KeyEvent.KEYCODE_BUTTON_R2: return R2;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return L3;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return R3;
            case KeyEvent.KEYCODE_BUTTON_START: return START;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return SELECT;
            case KeyEvent.KEYCODE_BUTTON_MODE: return HOME;
            default: return -1;
        }
    }

    /** "Match DS positions": the right face button is A and the bottom one B, as on a DS */
    static boolean byPosition(SharedPreferences p) { return p.getInt("padLayout", 0) == 1; }

    static int defaultAction(SharedPreferences p, int b) {
        boolean pos = byPosition(p);
        switch (b) {
            case A: return pos ? DS_B : DS_A;
            case B: return pos ? DS_A : DS_B;
            case X: return pos ? DS_Y : DS_X;
            case Y: return pos ? DS_X : DS_Y;
            case L1: case L2: return DS_L;
            case R1: case R2: return DS_R;
            case START: return DS_START;
            case SELECT: return DS_SELECT;
            case HOME: case L3: return MENU;
            case R3: return FFWD;
            default: return NONE;
        }
    }

    static int action(SharedPreferences p, int b) { return p.getInt("pad_" + b, defaultAction(p, b)); }
    static void setAction(SharedPreferences p, int b, int a) { p.edit().putInt("pad_" + b, a).apply(); }
    static void reset(SharedPreferences p) {
        SharedPreferences.Editor e = p.edit();
        for (int b = 0; b < COUNT; b++) e.remove("pad_" + b);
        e.apply();
    }

    static boolean isGamepad(InputDevice d) {
        if (d == null || d.isVirtual()) return false;
        int s = d.getSources();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /** name of a connected controller, or null */
    static String connected(InputManager im) {
        for (int id : im.getInputDeviceIds()) {
            InputDevice d = im.getInputDevice(id);
            if (isGamepad(d)) return d.getName();
        }
        return null;
    }

    private Pad() {}
}
