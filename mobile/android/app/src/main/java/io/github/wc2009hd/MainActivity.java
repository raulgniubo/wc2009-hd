package io.github.wc2009hd;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.method.LinkMovementMethod;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class MainActivity extends Activity implements GameView.Host, ControlsView.Listener {
    private static final int REQ_FOLDER = 1, REQ_PERMS = 2;
    private static final int AUTO_SLOT = 0;   // "resume where you left off" state

    private SharedPreferences prefs;
    private final Layout layout = new Layout();
    private final Input input = new Input();
    private GameView game;
    private ControlsView controls;
    private FrameLayout root;
    private boolean inGame;

    // ---------------------------------------------------------------- lifecycle

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("wc09", MODE_PRIVATE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        setContentView(root);
        copyFonts();
        showSetupOrGame();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (inGame) {
            prefs.edit().putBoolean("running", true).apply();
            game.onResume();
            game.setPaused(false);
            Native.audioStart();
        } else {
            showSetupOrGame();   // came back from the permission screen
        }
    }

    @Override
    protected void onPause() {
        if (inGame) {
            game.setPaused(true);
            input.releaseAll();
            Native.audioStop();
            // remember where we are (the process may be killed in the background)
            runOnGlAndWait(() -> { Native.saveState(AUTO_SLOT); Native.flushSave(); });
            prefs.edit().putBoolean("running", false).apply();
            game.onPause();
        }
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) hideSystemBars();
    }

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    private void runOnGlAndWait(Runnable r) {
        CountDownLatch done = new CountDownLatch(1);
        game.queueEvent(() -> { try { r.run(); } finally { done.countDown(); } });
        try { done.await(3, TimeUnit.SECONDS); } catch (InterruptedException ignored) {}
    }

    // ---------------------------------------------------------------- setup screen

    private boolean hasFileAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private String gameDir() { return prefs.getString("gameDir", ""); }

    private static File findRom(String dir) {
        File[] fs = new File(dir).listFiles();
        if (fs == null) return null;
        for (File f : fs)
            if (f.isFile() && f.getName().toLowerCase().endsWith(".nds") && !f.getName().startsWith(".")) return f;
        return null;
    }

    private void showSetupOrGame() {
        if (inGame) return;
        if (hasFileAccess() && !gameDir().isEmpty() && findRom(gameDir()) != null) startGameView();
        else showSetup();
    }

    private void showSetup() {
        root.removeAllViews();
        float dp = getResources().getDisplayMetrics().density;
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = (int) (28 * dp);
        col.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Yu-Gi-Oh! 5D's WC2009 HD");
        title.setTextColor(Color.WHITE);
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        col.addView(title);

        boolean access = hasFileAccess();
        String dir = gameDir();
        boolean romOk = !dir.isEmpty() && access && findRom(dir) != null;

        TextView body = new TextView(this);
        body.setTextColor(0xFFC8CDD6);
        body.setTextSize(16);
        body.setLineSpacing(0, 1.25f);
        StringBuilder s = new StringBuilder();
        s.append("\nPut the game folder on your phone (the .nds file plus the HDCards folder, ")
         .append("for example in Download/Yu-Gi-Oh 5Ds WC2009), then:\n\n");
        s.append(access ? "1. File access: allowed ✓\n" : "1. Allow file access, so the game can read the folder and save next to it.\n");
        if (!dir.isEmpty() && !romOk && access) s.append("\nNo .nds file found in:\n").append(dir).append("\n");
        s.append("\n2. Choose the game folder.\n\nIn-game saves and save states go to a Saves folder inside it.");
        body.setText(s);
        col.addView(body);

        if (!access) col.addView(button("Allow file access", v -> requestAccess(), dp));
        Button pick = button(dir.isEmpty() ? "Choose game folder" : "Choose another folder", v -> pickFolder(), dp);
        pick.setEnabled(access);
        col.addView(pick);

        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        root.addView(sv, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private Button button(String label, View.OnClickListener l, float dp) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(17);
        b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = (int) (16 * dp);
        b.setLayoutParams(lp);
        return b;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQ_PERMS);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        showSetupOrGame();
    }

    private void pickFolder() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        startActivityForResult(i, REQ_FOLDER);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_FOLDER || res != RESULT_OK || data == null || data.getData() == null) return;
        String path = treeToPath(data.getData());
        if (path == null) { toast("That folder can't be used, pick one in the phone storage or SD card"); return; }
        prefs.edit().putString("gameDir", path).apply();
        if (findRom(path) == null) { toast("No .nds file in that folder"); showSetup(); return; }
        showSetupOrGame();
    }

    /** content://.../tree/primary:Download/Game -> /storage/emulated/0/Download/Game */
    private static String treeToPath(Uri tree) {
        String id;
        try { id = DocumentsContract.getTreeDocumentId(tree); } catch (Exception e) { return null; }
        int colon = id.indexOf(':');
        if (colon < 0) return null;
        String vol = id.substring(0, colon), rel = id.substring(colon + 1);
        String base = vol.equalsIgnoreCase("primary") ? Environment.getExternalStorageDirectory().getAbsolutePath()
                : "/storage/" + vol;
        return rel.isEmpty() ? base : base + "/" + rel;
    }

    private void copyFonts() {
        File dir = new File(getFilesDir(), "fonts");
        dir.mkdirs();
        for (String name : new String[] {"Inter-Medium.ttf", "Inter-Bold.ttf"}) {
            File out = new File(dir, name);
            if (out.exists() && out.length() > 0) continue;
            try (InputStream in = getAssets().open("fonts/" + name); OutputStream os = new FileOutputStream(out)) {
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            } catch (Exception e) { out.delete(); }
        }
    }

    // ---------------------------------------------------------------- game

    private void startGameView() {
        root.removeAllViews();
        game = new GameView(this, this, layout, input);
        controls = new ControlsView(this, layout, input, this);
        controls.setShowButtons(prefs.getBoolean("buttons", true));
        controls.setOpacity(prefs.getFloat("opacity", 0.55f));
        root.addView(game, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(controls, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addOnLayoutChangeListener((v, l, t, r, btm, ol, ot, or, ob) -> relayout());
        inGame = true;
        relayout();
        Native.audioStart();
    }

    private void relayout() {
        if (controls == null) return;
        layout.compute(root.getWidth(), root.getHeight(), getResources().getDisplayMetrics().density,
                prefs.getBoolean("buttons", true));
        controls.invalidate();
    }

    /** GL thread. */
    @Override
    public String startGame() {
        String dir = gameDir();
        File rom = findRom(dir);
        if (rom == null) return "No .nds file found in " + dir;
        boolean jit = prefs.getBoolean("jit", true);
        if (jit && prefs.getBoolean("running", false)) {
            // the last session ended in a crash with the JIT on: play it safe
            jit = false;
            prefs.edit().putBoolean("jit", false).apply();
            runOnUiThread(() -> toast("The last session crashed, so the fast CPU mode (JIT) was turned off. You can turn it back on in the menu."));
        }
        prefs.edit().putBoolean("running", true).commit();
        Native.init(dir, getFilesDir().getAbsolutePath() + "/data", getFilesDir().getAbsolutePath() + "/fonts");
        int r = Native.loadRom(rom.getAbsolutePath(), prefs.getInt("scale", 4), jit);
        if (r != 0) return "Couldn't load " + rom.getName() + " (error " + r + ")";
        Native.everLoaded = true;
        if (Native.stateExists(AUTO_SLOT) && Native.loadState(AUTO_SLOT) == 0)
            runOnUiThread(() -> toast("Resumed where you left off"));
        return null;
    }

    @Override
    public void onStartFailed(String message) {
        runOnUiThread(() -> {
            inGame = false;
            prefs.edit().putBoolean("running", false).apply();
            new AlertDialog.Builder(this).setTitle("Couldn't start").setMessage(message)
                    .setPositiveButton("OK", (d, w) -> { root.removeAllViews(); showSetup(); }).show();
        });
    }

    @Override
    public void onFastForward(boolean on) { game.setFastForward(on); }

    @Override
    public void onMenu() {
        game.setPaused(true);
        String[] items = {
                "Save state…", "Load state…", "Resolution: " + prefs.getInt("scale", 4) + "x",
                prefs.getBoolean("buttons", true) ? "Hide buttons" : "Show buttons",
                "Button opacity", "Fast CPU mode (JIT): " + (prefs.getBoolean("jit", true) ? "on" : "off"),
                "Restart from in-game save", "Change game folder"
        };
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("WC2009 HD")
                .setItems(items, (d, which) -> {
                    switch (which) {
                        case 0: stateDialog(true); return;
                        case 1: stateDialog(false); return;
                        case 2: scaleDialog(); return;
                        case 3: toggleButtons(); break;
                        case 4: opacityDialog(); return;
                        case 5: toggleJit(); break;
                        case 6: restart(); break;
                        case 7: changeFolder(); return;
                    }
                    game.setPaused(false);
                })
                .setOnCancelListener(d -> game.setPaused(false))
                .show();
    }

    private void stateDialog(boolean save) {
        String[] slots = new String[8];
        for (int i = 0; i < 8; i++) slots[i] = "Slot " + (i + 1) + (Native.stateExists(i + 1) ? "" : "  (empty)");
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(save ? "Save state" : "Load state")
                .setItems(slots, (d, which) -> {
                    int slot = which + 1;
                    game.queueEvent(() -> {
                        int r = save ? Native.saveState(slot) : Native.loadState(slot);
                        runOnUiThread(() -> toast(r == 0 ? (save ? "Saved to slot " : "Loaded slot ") + slot
                                : (save ? "Couldn't save" : "Couldn't load slot " + slot)));
                    });
                    game.setPaused(false);
                })
                .setOnCancelListener(d -> game.setPaused(false))
                .show();
    }

    private void scaleDialog() {
        final int[] scales = {2, 3, 4, 5, 6, 8};
        String[] names = {"2x (fastest)", "3x", "4x (recommended)", "5x", "6x (sharp, needs a fast phone)", "8x (sharpest, high-end phones)"};
        int cur = prefs.getInt("scale", 4), sel = 2;
        for (int i = 0; i < scales.length; i++) if (scales[i] == cur) sel = i;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Resolution")
                .setSingleChoiceItems(names, sel, (d, which) -> {
                    int s = scales[which];
                    prefs.edit().putInt("scale", s).apply();
                    game.queueEvent(() -> Native.setScale(s));
                    d.dismiss();
                    game.setPaused(false);
                })
                .setOnCancelListener(d -> game.setPaused(false))
                .show();
    }

    private void toggleButtons() {
        boolean on = !prefs.getBoolean("buttons", true);
        prefs.edit().putBoolean("buttons", on).apply();
        controls.setShowButtons(on);
        relayout();
    }

    private void opacityDialog() {
        final float[] vals = {0.25f, 0.4f, 0.55f, 0.75f, 0.95f};
        String[] names = {"25%", "40%", "55%", "75%", "95%"};
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Button opacity")
                .setItems(names, (d, which) -> {
                    prefs.edit().putFloat("opacity", vals[which]).apply();
                    controls.setOpacity(vals[which]);
                    game.setPaused(false);
                })
                .setOnCancelListener(d -> game.setPaused(false))
                .show();
    }

    private void toggleJit() {
        boolean on = !prefs.getBoolean("jit", true);
        prefs.edit().putBoolean("jit", on).apply();
        toast("Takes effect next time the app starts");
    }

    private void restart() {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Restart the game?")
                .setMessage("Starts again from your last in-game save. Anything after that save is lost (save states stay).")
                .setPositiveButton("Restart", (d, w) -> game.queueEvent(() -> {
                    Native.deleteState(AUTO_SLOT);
                    File rom = findRom(gameDir());
                    if (rom != null) Native.loadRom(rom.getAbsolutePath(), prefs.getInt("scale", 4), prefs.getBoolean("jit", true));
                    game.setPaused(false);
                }))
                .setNegativeButton("Cancel", (d, w) -> game.setPaused(false))
                .setOnCancelListener(d -> game.setPaused(false))
                .show();
    }

    private void changeFolder() {
        // leave the game (remembering where it was), then pick; cancelling comes back to the same game
        runOnGlAndWait(() -> { Native.saveState(AUTO_SLOT); Native.flushSave(); });
        Native.audioStop();
        input.releaseAll();
        inGame = false;
        game.onPause();
        root.removeAllViews();
        game = null; controls = null;
        prefs.edit().putBoolean("running", false).apply();
        pickFolder();
    }

    private void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_LONG).show(); }

    // ---------------------------------------------------------------- gamepads

    private int padKeys;

    private static int mapKey(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: return Native.A;
            case KeyEvent.KEYCODE_BUTTON_B: return Native.B;
            case KeyEvent.KEYCODE_BUTTON_X: return Native.X;
            case KeyEvent.KEYCODE_BUTTON_Y: return Native.Y;
            case KeyEvent.KEYCODE_BUTTON_L1: return Native.L;
            case KeyEvent.KEYCODE_BUTTON_R1: return Native.R;
            case KeyEvent.KEYCODE_BUTTON_START: return Native.START;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return Native.SELECT;
            case KeyEvent.KEYCODE_DPAD_UP: return Native.UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return Native.DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return Native.LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return Native.RIGHT;
            default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (inGame) {
            if (e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_MODE && e.getAction() == KeyEvent.ACTION_DOWN) { onMenu(); return true; }
            int k = mapKey(e.getKeyCode());
            if (k != 0) {
                if (e.getAction() == KeyEvent.ACTION_DOWN) padKeys |= k;
                else if (e.getAction() == KeyEvent.ACTION_UP) padKeys &= ~k;
                input.setPadKeys(padKeys | stickKeys);
                return true;
            }
        }
        return super.dispatchKeyEvent(e);
    }

    private int stickKeys;

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent e) {
        if (inGame && (e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                && e.getAction() == MotionEvent.ACTION_MOVE) {
            float x = e.getAxisValue(MotionEvent.AXIS_HAT_X) + e.getAxisValue(MotionEvent.AXIS_X);
            float y = e.getAxisValue(MotionEvent.AXIS_HAT_Y) + e.getAxisValue(MotionEvent.AXIS_Y);
            int k = 0;
            if (x < -0.5f) k |= Native.LEFT;
            if (x > 0.5f) k |= Native.RIGHT;
            if (y < -0.5f) k |= Native.UP;
            if (y > 0.5f) k |= Native.DOWN;
            float lt = e.getAxisValue(MotionEvent.AXIS_LTRIGGER), rt = e.getAxisValue(MotionEvent.AXIS_RTRIGGER);
            if (lt > 0.5f) k |= Native.L;
            if (rt > 0.5f) k |= Native.R;
            stickKeys = k;
            input.setPadKeys(padKeys | stickKeys);
            return true;
        }
        return super.dispatchGenericMotionEvent(e);
    }

    @Override
    public void onBackPressed() {
        if (inGame) onMenu();
        else super.onBackPressed();
    }
}
