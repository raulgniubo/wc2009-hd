package io.github.wc2009hd;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
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

public final class MainActivity extends Activity
        implements GameView.Host, ControlsView.Listener, LibraryView.Listener, SettingsView.Listener, EnhancementsView.Listener {
    private static final int REQ_FOLDER = 1, REQ_PERMS = 2, REQ_COVER = 3, REQ_IMPORT = 4;
    private LibraryView.Game coverTarget;
    private EnhancementsView enhancements;
    private static final int AUTO_SLOT = 0;   // the resume point

    private SharedPreferences prefs;
    private final Layout layout = new Layout();
    private final Input input = new Input();
    private FrameLayout root;

    private enum Screen { SETUP, LIBRARY, SETTINGS, GAME }
    private Screen screen = Screen.SETUP;
    private LibraryView library;
    private SettingsView settings;      // over the library or over the game
    private GameView game;
    private ControlsView controls;
    private File currentRom;
    private boolean freshStart;

    // ================================================================ lifecycle

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("wc09", MODE_PRIVATE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(Ui.BG);
        getWindow().setNavigationBarColor(Ui.BG);
        root = new FrameLayout(this);
        root.setBackgroundColor(Ui.BG);
        setContentView(root);
        copyFonts();
        route();
    }

    @Override
    protected void onResume() {
        super.onResume();
        applySystemBars();
        if (game != null) {
            prefs.edit().putBoolean("running", true).apply();
            game.onResume();
            if (settings == null) game.setPaused(false);
            if (SettingsView.on(prefs, "sound")) Native.audioStart();
        } else if (screen == Screen.SETUP) {
            route();   // back from the permission screen
        } else if (screen == Screen.LIBRARY && library != null) {
            library.load(gameDir());   // games may have been added
        }
    }

    @Override
    protected void onPause() {
        if (game != null) {
            game.setPaused(true);
            input.releaseAll();
            Native.audioStop();
            boolean resume = SettingsView.on(prefs, "resume");
            runOnGlAndWait(() -> { if (resume) Native.saveState(AUTO_SLOT); Native.flushSave(); });
            prefs.edit().putBoolean("running", false).apply();
            game.onPause();
        }
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) applySystemBars();
    }

    /** Full screen while playing; normal bars in the library and settings. */
    private void applySystemBars() {
        boolean full = game != null;
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c == null) return;
            if (full) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            } else {
                c.show(WindowInsets.Type.systemBars());
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(full ? View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN : 0);
        }
    }

    private void runOnGlAndWait(Runnable r) {
        if (game == null) { r.run(); return; }
        CountDownLatch done = new CountDownLatch(1);
        game.queueEvent(() -> { try { r.run(); } finally { done.countDown(); } });
        try { done.await(3, TimeUnit.SECONDS); } catch (InterruptedException ignored) {}
    }

    @Override
    public void onBackPressed() {
        if (enhancements != null) closeEnhancements();
        else if (settings != null) closeSettings();
        else if (game != null) onMenu();
        else super.onBackPressed();
    }

    // ================================================================ routing

    private String gameDir() { return prefs.getString("gameDir", ""); }

    private boolean hasFileAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void route() {
        if (game != null) return;
        if (hasFileAccess() && !gameDir().isEmpty() && new File(gameDir()).isDirectory()) showLibrary();
        else showSetup();
    }

    private void showLibrary() {
        screen = Screen.LIBRARY;
        settings = null;
        root.removeAllViews();
        library = new LibraryView(this, prefs, this);
        root.addView(library, new FrameLayout.LayoutParams(-1, -1));
        library.load(gameDir());
        applySystemBars();
    }

    // ================================================================ first-run setup

    private void showSetup() {
        screen = Screen.SETUP;
        root.removeAllViews();
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        int pad = Ui.dp(this, 28);
        col.setPadding(pad, Ui.dp(this, 56), pad, pad);

        col.addView(Ui.text(this, "Welcome", 32, Ui.TEXT, true));
        TextView sub = Ui.text(this, "Two quick steps and your games are ready.", 16, Ui.TEXT_DIM, false);
        sub.setPadding(0, Ui.dp(this, 6), 0, Ui.dp(this, 22));
        col.addView(sub);

        boolean access = hasFileAccess();
        col.addView(step("1", "Allow file access", access ? "Done." : "So the app can read your games and keep saves next to them.",
                access, access ? null : (v -> requestAccess())));
        String dir = gameDir();
        col.addView(step("2", "Choose your games folder",
                "A folder with your .nds files, for example Download/DS Games. For Yu-Gi-Oh! 5D's WC2009, put its HDCards folder in there too."
                        + (dir.isEmpty() ? "" : "\n\nCurrent: " + dir),
                false, access ? (v -> pickFolder()) : null));

        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        root.addView(sv, new FrameLayout.LayoutParams(-1, -1));
    }

    private View step(String num, String title, String body, boolean done, View.OnClickListener action) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.HORIZONTAL);
        card.setPadding(Ui.dp(this, 18), Ui.dp(this, 18), Ui.dp(this, 18), Ui.dp(this, 18));
        card.setBackground(action != null ? Ui.pressable(this, Ui.CARD, 18) : Ui.rounded(Ui.CARD, Ui.dp(this, 18)));
        if (action != null) card.setOnClickListener(action);
        else if (!done) card.setAlpha(0.5f);
        TextView n = Ui.text(this, done ? "✓" : num, 17, done ? 0xFF06111F : Ui.TEXT, true);
        n.setGravity(Gravity.CENTER);
        n.setBackground(Ui.rounded(done ? Ui.ACCENT : Ui.CARD_HI, Ui.dp(this, 18)));
        card.addView(n, new LinearLayout.LayoutParams(Ui.dp(this, 36), Ui.dp(this, 36)));
        LinearLayout txt = new LinearLayout(this);
        txt.setOrientation(LinearLayout.VERTICAL);
        txt.setPadding(Ui.dp(this, 16), 0, 0, 0);
        txt.addView(Ui.text(this, title, 17, action != null ? Ui.ACCENT : Ui.TEXT, true));
        TextView b = Ui.text(this, body, 14, Ui.TEXT_DIM, false);
        b.setPadding(0, Ui.dp(this, 4), 0, 0);
        b.setLineSpacing(0, 1.2f);
        txt.addView(b);
        card.addView(txt, new LinearLayout.LayoutParams(0, -2, 1));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = Ui.dp(this, 14);
        card.setLayoutParams(lp);
        return card;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQ_PERMS);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        route();
    }

    private void pickFolder() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_FOLDER);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_IMPORT) {
            if (res != RESULT_OK || data == null) return;
            java.util.List<Uri> uris = new java.util.ArrayList<>();
            if (data.getClipData() != null)
                for (int k = 0; k < data.getClipData().getItemCount(); k++) uris.add(data.getClipData().getItemAt(k).getUri());
            else if (data.getData() != null) uris.add(data.getData());
            if (!uris.isEmpty()) importUris(uris);
            return;
        }
        if (req == REQ_COVER) {
            if (res == RESULT_OK && data != null && data.getData() != null && coverTarget != null) saveCover(coverTarget, data.getData());
            return;
        }
        if (req != REQ_FOLDER || res != RESULT_OK || data == null || data.getData() == null) return;
        String path = treeToPath(data.getData());
        if (path == null || !new File(path).isDirectory()) {
            toast("That folder can't be used. Pick one in the phone's storage or on the SD card.");
            return;
        }
        prefs.edit().putString("gameDir", path).apply();
        showLibrary();
    }

    /** content://.../tree/primary:Download/Games -> /storage/emulated/0/Download/Games */
    private static String treeToPath(Uri tree) {
        String id;
        try { id = DocumentsContract.getTreeDocumentId(tree); } catch (Exception e) { return null; }
        int colon = id.indexOf(':');
        if (colon < 0) return null;
        String vol = id.substring(0, colon), rel = id.substring(colon + 1);
        String base = vol.equalsIgnoreCase("primary") ? Environment.getExternalStorageDirectory().getAbsolutePath() : "/storage/" + vol;
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

    // ================================================================ library

    @Override
    public void play(LibraryView.Game g, boolean fresh) {
        prefs.edit().putLong("played:" + g.rom.getName(), System.currentTimeMillis()).apply();
        currentRom = g.rom;
        freshStart = fresh;
        startGameView();
    }

    @Override
    public void gameOptions(LibraryView.Game g) {
        java.util.List<String> items = new java.util.ArrayList<>();
        java.util.List<Runnable> acts = new java.util.ArrayList<>();
        items.add("Play"); acts.add(() -> play(g, false));
        if (g.resume) { items.add("Start from in-game save"); acts.add(() -> play(g, true)); }
        items.add(g.favorite ? "Remove from favorites" : "Add to favorites");
        acts.add(() -> { prefs.edit().putBoolean("fav:" + g.rom.getName(), !g.favorite).apply(); library.load(gameDir()); });
        items.add("Change cover…"); acts.add(() -> pickCover(g));
        if (g.cover != null) { items.add("Use game icon"); acts.add(() -> { removeCovers(g); library.load(gameDir()); }); }
        items.add("Rename…"); acts.add(() -> rename(g));
        if (g.resume) {
            items.add("Remove resume point");
            acts.add(() -> { new File(gameDir() + "/Saves/" + g.base + ".ml0").delete(); library.load(gameDir()); });
        }
        items.add("Delete game…"); acts.add(() -> confirmDelete(g));
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(g.title)
                .setItems(items.toArray(new String[0]), (d, which) -> acts.get(which).run())
                .show();
    }

    @Override
    public void chooseFolder() { pickFolder(); }

    @Override
    public void importGames() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(i, REQ_IMPORT);
    }

    /** Copies the picked .nds files into the games folder. */
    private void importUris(java.util.List<Uri> uris) {
        toast("Importing…");
        String dir = gameDir();
        new Thread(() -> {
            int n = 0, skipped = 0;
            for (Uri u : uris) {
                String name = null;
                try (android.database.Cursor cur = getContentResolver().query(u, new String[] {android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                    if (cur != null && cur.moveToFirst()) name = cur.getString(0);
                } catch (Exception ignored) {}
                if (name == null || !name.toLowerCase().endsWith(".nds")) { skipped++; continue; }
                File out = new File(dir, name);
                if (out.exists()) continue;
                try (InputStream in = getContentResolver().openInputStream(u); OutputStream os = new FileOutputStream(out)) {
                    byte[] buf = new byte[1 << 20];
                    int r;
                    while ((r = in.read(buf)) > 0) os.write(buf, 0, r);
                    n++;
                } catch (Exception e) { out.delete(); }
            }
            final int added = n, other = skipped;
            runOnUiThread(() -> {
                toast(added == 1 ? "1 game added" : added + " games added" + (other > 0 ? " (only .nds files can be added)" : ""));
                if (library != null) library.load(dir);
            });
        }).start();
    }

    private void pickCover(LibraryView.Game g) {
        coverTarget = g;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("image/*");
        startActivityForResult(i, REQ_COVER);
    }

    private void removeCovers(LibraryView.Game g) {
        for (String d : new String[] {gameDir() + "/Covers", gameDir()})
            for (String ext : new String[] {".png", ".jpg", ".jpeg", ".webp"})
                new File(d, g.base + ext).delete();
    }

    private void saveCover(LibraryView.Game g, Uri uri) {
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            android.graphics.Bitmap bm = android.graphics.BitmapFactory.decodeStream(in);
            if (bm == null) { toast("That picture can't be used."); return; }
            float k = 1024f / Math.max(bm.getWidth(), bm.getHeight());
            if (k < 1) bm = android.graphics.Bitmap.createScaledBitmap(bm, Math.round(bm.getWidth() * k), Math.round(bm.getHeight() * k), true);
            removeCovers(g);
            File dir = new File(gameDir(), "Covers");
            dir.mkdirs();
            try (OutputStream os = new FileOutputStream(new File(dir, g.base + ".jpg"))) {
                bm.compress(android.graphics.Bitmap.CompressFormat.JPEG, 90, os);
            }
            library.load(gameDir());
        } catch (Exception e) {
            toast("Couldn't save the cover.");
        }
    }

    private void rename(LibraryView.Game g) {
        android.widget.EditText et = new android.widget.EditText(this);
        et.setText(g.title);
        et.setSelectAllOnFocus(true);
        et.setSingleLine(true);
        FrameLayout box = new FrameLayout(this);
        box.setPadding(Ui.dp(this, 20), Ui.dp(this, 8), Ui.dp(this, 20), 0);
        box.addView(et);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Rename")
                .setView(box)
                .setPositiveButton("Save", (d, w) -> {
                    String t = et.getText().toString().trim();
                    if (t.isEmpty()) prefs.edit().remove("name:" + g.rom.getName()).apply();
                    else prefs.edit().putString("name:" + g.rom.getName(), t).apply();
                    library.load(gameDir());
                })
                .setNeutralButton("Reset", (d, w) -> { prefs.edit().remove("name:" + g.rom.getName()).apply(); library.load(gameDir()); })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void confirmDelete(LibraryView.Game g) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Delete " + g.title + "?")
                .setMessage("The game file is removed from this phone. Your saves stay in the Saves folder.")
                .setPositiveButton("Delete", (d, w) -> { g.rom.delete(); library.load(gameDir()); })
                .setNegativeButton("Cancel", null)
                .show();
    }

    @Override
    public void openEnhancements(Packs.Pack pack) {
        enhancements = new EnhancementsView(this, prefs, pack, this, gameDir());
        root.addView(enhancements, new FrameLayout.LayoutParams(-1, -1));
    }

    @Override
    public void closeEnhancements() {
        if (enhancements == null) return;
        root.removeView(enhancements);
        enhancements = null;
        // refresh the On/Off value on the settings page
        if (settings != null) {
            int idx = root.indexOfChild(settings);
            root.removeView(settings);
            settings = new SettingsView(this, prefs, this, gameDir());
            root.addView(settings, idx, new FrameLayout.LayoutParams(-1, -1));
        }
    }

    // ================================================================ settings

    @Override
    public void openSettings() {
        settings = new SettingsView(this, prefs, this, gameDir());
        if (game != null) {
            game.setPaused(true);
            root.addView(settings, new FrameLayout.LayoutParams(-1, -1));
        } else {
            screen = Screen.SETTINGS;
            root.removeAllViews();
            root.addView(settings, new FrameLayout.LayoutParams(-1, -1));
        }
    }

    @Override
    public void closeSettings() {
        if (enhancements != null) { root.removeView(enhancements); enhancements = null; }
        if (game != null) {
            root.removeView(settings);
            settings = null;
            game.setPaused(false);
            applySystemBars();
        } else {
            showLibrary();
        }
    }

    @Override
    public void changeFolder() {
        if (game != null) { closeSettings(); leaveGame(); }
        pickFolder();
    }

    @Override
    public void settingChanged(String key) {
        if (game == null) return;
        switch (key) {
            case "scale": { int s = SettingsView.scale(prefs); game.queueEvent(() -> Native.setScale(s)); break; }
            case "filter": case "smooth2d": case "hdCards": case "hdText": case "hdAll":
                game.queueEvent(this::applyOptions); break;
            case "buttons": controls.setShowButtons(SettingsView.on(prefs, "buttons")); relayout(); break;
            case "opacity": controls.setOpacity(SettingsView.opacity(prefs)); break;
            case "haptics": controls.setHaptics(SettingsView.on(prefs, "haptics")); break;
            case "sound": Native.setMuted(!SettingsView.on(prefs, "sound")); break;
            case "ffSpeed": game.setFastForwardSpeed(SettingsView.ffSpeed(prefs)); break;
        }
    }

    private void applyOptions() {
        Native.setOption(Native.OPT_HD_CARDS, SettingsView.on(prefs, "hdCards") ? 1 : 0);
        Native.setOption(Native.OPT_HD_TEXT, SettingsView.on(prefs, "hdText") ? 1 : 0);
        Native.setOption(Native.OPT_SMOOTH_2D, SettingsView.on(prefs, "smooth2d") ? 1 : 0);
        Native.setOption(Native.OPT_LINEAR_FILTER, SettingsView.on(prefs, "filter") ? 1 : 0);
        Native.setOption(Native.OPT_HD_ALL, SettingsView.on(prefs, "hdAll") ? 1 : 0);
    }

    // ================================================================ game

    private void startGameView() {
        screen = Screen.GAME;
        root.removeAllViews();
        settings = null;
        game = new GameView(this, this, layout, input);
        game.setFastForwardSpeed(SettingsView.ffSpeed(prefs));
        controls = new ControlsView(this, layout, input, this);
        controls.setShowButtons(SettingsView.on(prefs, "buttons"));
        controls.setOpacity(SettingsView.opacity(prefs));
        controls.setHaptics(SettingsView.on(prefs, "haptics"));
        root.setBackgroundColor(Color.BLACK);
        root.addView(game, new FrameLayout.LayoutParams(-1, -1));
        root.addView(controls, new FrameLayout.LayoutParams(-1, -1));
        root.addOnLayoutChangeListener(relayoutListener);
        relayout();
        Native.setMuted(!SettingsView.on(prefs, "sound"));
        Native.audioStart();
        applySystemBars();
    }

    private final View.OnLayoutChangeListener relayoutListener = (v, l, t, r, b, ol, ot, or, ob) -> relayout();

    private void relayout() {
        if (controls == null) return;
        layout.compute(root.getWidth(), root.getHeight(), getResources().getDisplayMetrics().density, SettingsView.on(prefs, "buttons"));
        controls.invalidate();
    }

    /** GL thread. */
    @Override
    public String startGame() {
        File rom = currentRom;
        if (rom == null || !rom.exists()) return "The game file isn't there anymore.";
        boolean jit = SettingsView.on(prefs, "jit");
        if (jit && prefs.getBoolean("running", false)) {
            jit = false;
            prefs.edit().putBoolean("jit", false).apply();
            runOnUiThread(() -> toast("The last session crashed, so Fast CPU mode was turned off. You can turn it back on in Settings."));
        }
        prefs.edit().putBoolean("running", true).commit();
        Native.init(gameDir(), getFilesDir().getAbsolutePath() + "/data", getFilesDir().getAbsolutePath() + "/fonts");
        applyOptions();
        int r = Native.loadRom(rom.getAbsolutePath(), SettingsView.scale(prefs), jit);
        if (r != 0) return "Couldn't load " + rom.getName() + " (error " + r + ").";
        Native.everLoaded = true;
        if (!freshStart && SettingsView.on(prefs, "resume") && Native.stateExists(AUTO_SLOT) && Native.loadState(AUTO_SLOT) == 0)
            runOnUiThread(() -> toast("Resumed where you left off"));
        return null;
    }

    @Override
    public void onStartFailed(String message) {
        runOnUiThread(() -> {
            prefs.edit().putBoolean("running", false).apply();
            closeGame();
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Couldn't start the game").setMessage(message).setPositiveButton("OK", null).show();
        });
    }

    /** Saves the resume point, shuts the emulator down and goes back to the library. */
    private void leaveGame() {
        if (game == null) return;
        boolean resume = SettingsView.on(prefs, "resume");
        runOnGlAndWait(() -> {
            if (resume) Native.saveState(AUTO_SLOT); else Native.deleteState(AUTO_SLOT);
            Native.flushSave();
            Native.shutdown();
        });
        prefs.edit().putBoolean("running", false).apply();
        closeGame();
    }

    private void closeGame() {
        Native.audioStop();
        input.releaseAll();
        if (game != null) game.onPause();
        root.removeOnLayoutChangeListener(relayoutListener);
        game = null; controls = null;
        root.setBackgroundColor(Ui.BG);
        showLibrary();
    }

    @Override
    public void onFastForward(boolean on) { game.setFastForward(on); }

    @Override
    public void onMenu() {
        if (game == null || settings != null) return;
        game.setPaused(true);
        boolean hdGame = Native.isHdGame(), hd = SettingsView.on(prefs, "hdAll");
        boolean full = !SettingsView.on(prefs, "buttons");
        String fs = full ? "Exit full screen" : "Full screen (hide buttons)";
        String[] items = hdGame
                ? new String[] {fs, "Save state", "Load state", hd ? "Switch to original look" : "Switch to HD look", "Settings", "Restart from in-game save", "Back to library"}
                : new String[] {fs, "Save state", "Load state", "Settings", "Restart from in-game save", "Back to library"};
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(currentRom != null ? currentRom.getName().replaceAll("\\.nds$", "") : "Menu")
                .setItems(items, (d, which) -> {
                    if (which == 0) {
                        prefs.edit().putBoolean("buttons", full).apply();
                        settingChanged("buttons");
                        game.setPaused(false);
                        return;
                    }
                    which--;
                    if (hdGame) {
                        if (which == 2) {
                            prefs.edit().putBoolean("hdAll", !hd).apply();
                            settingChanged("hdAll");
                            toast(hd ? "Original look" : "HD look");
                            game.setPaused(false);
                            return;
                        }
                        if (which > 2) which--;
                    }
                    switch (which) {
                        case 0: stateDialog(true); return;
                        case 1: stateDialog(false); return;
                        case 2: openSettings(); return;
                        case 3: restart(); return;
                        case 4: leaveGame(); return;
                    }
                })
                .setOnCancelListener(d -> game.setPaused(false))
                .show();
    }

    private void stateDialog(boolean save) {
        String[] slots = new String[8];
        for (int i = 0; i < 8; i++) slots[i] = "Slot " + (i + 1) + (Native.stateExists(i + 1) ? "" : "   (empty)");
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

    private void restart() {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Restart the game?")
                .setMessage("Starts again from your last in-game save. Anything after it is lost (save states are kept).")
                .setPositiveButton("Restart", (d, w) -> game.queueEvent(() -> {
                    Native.deleteState(AUTO_SLOT);
                    if (currentRom != null) Native.loadRom(currentRom.getAbsolutePath(), SettingsView.scale(prefs), SettingsView.on(prefs, "jit"));
                    game.setPaused(false);
                }))
                .setNegativeButton("Cancel", (d, w) -> game.setPaused(false))
                .setOnCancelListener(d -> game.setPaused(false))
                .show();
    }

    private void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_LONG).show(); }

    // ================================================================ gamepads

    private int padKeys, stickKeys;

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
        if (game != null && settings == null) {
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

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent e) {
        if (game != null && (e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                && e.getAction() == MotionEvent.ACTION_MOVE) {
            float x = e.getAxisValue(MotionEvent.AXIS_HAT_X) + e.getAxisValue(MotionEvent.AXIS_X);
            float y = e.getAxisValue(MotionEvent.AXIS_HAT_Y) + e.getAxisValue(MotionEvent.AXIS_Y);
            int k = 0;
            if (x < -0.5f) k |= Native.LEFT;
            if (x > 0.5f) k |= Native.RIGHT;
            if (y < -0.5f) k |= Native.UP;
            if (y > 0.5f) k |= Native.DOWN;
            if (e.getAxisValue(MotionEvent.AXIS_LTRIGGER) > 0.5f) k |= Native.L;
            if (e.getAxisValue(MotionEvent.AXIS_RTRIGGER) > 0.5f) k |= Native.R;
            stickKeys = k;
            input.setPadKeys(padKeys | stickKeys);
            return true;
        }
        return super.dispatchGenericMotionEvent(e);
    }
}
