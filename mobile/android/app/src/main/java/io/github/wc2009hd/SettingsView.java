package io.github.wc2009hd;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

/** All the settings, grouped in cards. Changes are saved at once and reported to the listener. */
final class SettingsView extends LinearLayout {
    interface Listener {
        void settingChanged(String key);
        void closeSettings();
        void changeFolder();
        void openEnhancements(Packs.Pack pack);
        void addFolder();
        void removeFolder(String dir);
    }

    // keys and defaults
    static int scale(SharedPreferences p) { return p.getInt("scale", 4); }
    static boolean on(SharedPreferences p, String key) {
        switch (key) {
            case "boxart": case "hdAll": case "jit": case "filter": case "smooth2d": case "hdCards": case "hdText": case "buttons":
            case "haptics": case "resume": case "sound": return p.getBoolean(key, true);
            default: return p.getBoolean(key, false);
        }
    }
    static float opacity(SharedPreferences p) { return p.getFloat("opacity", 0.55f); }
    static int ffSpeed(SharedPreferences p) { return p.getInt("ffSpeed", 3); }

    private final SharedPreferences prefs;
    private final Listener listener;
    private final LinearLayout list;
    private final Context c;

    SettingsView(Context c, SharedPreferences prefs, Listener listener, String folder) {
        super(c);
        this.c = c; this.prefs = prefs; this.listener = listener;
        setOrientation(VERTICAL);
        setBackgroundColor(Ui.BG);
        setClickable(true);   // don't let touches through to the game underneath

        LinearLayout bar = new LinearLayout(c);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(Ui.dp(c, 8), Ui.dp(c, 14), Ui.dp(c, 20), Ui.dp(c, 6));
        TextView back = Ui.text(c, "‹  Back", 16, Ui.ACCENT, true);
        back.setPadding(Ui.dp(c, 12), Ui.dp(c, 8), Ui.dp(c, 14), Ui.dp(c, 8));
        back.setBackground(Ui.pressable(c, Ui.BG, 18));
        back.setOnClickListener(v -> listener.closeSettings());
        bar.addView(back);
        TextView title = Ui.text(c, "Settings", 20, Ui.TEXT, true);
        title.setGravity(Gravity.CENTER);
        bar.addView(title, new LayoutParams(0, -2, 1));
        View spacer = new View(c);
        bar.addView(spacer, new LayoutParams(Ui.dp(c, 70), 1));
        addView(bar);

        ScrollView sv = new ScrollView(c);
        list = new LinearLayout(c);
        list.setOrientation(VERTICAL);
        list.setPadding(Ui.dp(c, 16), Ui.dp(c, 4), Ui.dp(c, 16), Ui.dp(c, 32));
        sv.addView(list);
        addView(sv, new LayoutParams(-1, 0, 1));

        // ---- graphics
        LinearLayout g = section("Display");
        final int[] scales = {2, 3, 4, 5, 6, 8};
        int sel = 2;
        for (int i = 0; i < scales.length; i++) if (scales[i] == scale(prefs)) sel = i;
        g.addView(label("Resolution", "Higher is sharper. 4x looks great on most phones; 6x and 8x need a fast phone and use more battery."));
        g.addView(pad(Ui.chips(c, new String[] {"2x", "3x", "4x", "5x", "6x", "8x"}, sel, i -> {
            prefs.edit().putInt("scale", scales[i]).apply(); listener.settingChanged("scale");
        })));
        g.addView(Ui.divider(c));
        g.addView(label("Screen filter", "Smooth blends the picture to fit the screen; Sharp keeps hard pixel edges."));
        g.addView(pad(Ui.chips(c, new String[] {"Smooth", "Sharp"}, on(prefs, "filter") ? 0 : 1, i -> {
            prefs.edit().putBoolean("filter", i == 0).apply(); listener.settingChanged("filter");
        })));
        g.addView(Ui.divider(c));
        g.addView(toggle("smooth2d", "Smooth 2D graphics", "Upscales menus and sprites with smooth edges instead of blocky pixels."));
        g.addView(Ui.divider(c));
        g.addView(toggle("perf", "Show frame rate", "A small number in a corner of the game screen. 60 is full speed."));
        g.addView(Ui.divider(c));
        g.addView(toggle("perfDetail", "Detailed performance", "Shows frame timing and graphics stats next to the frame rate."));

        // ---- WC2009
        LinearLayout w = section("Game enhancements");
        for (int i = 0; i < Packs.ALL.length; i++) {
            Packs.Pack p = Packs.ALL[i];
            if (i > 0) w.addView(Ui.divider(c));
            LinearLayout row = new LinearLayout(c);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(Ui.dp(c, 16), Ui.dp(c, 16), Ui.dp(c, 16), Ui.dp(c, 16));
            row.setBackground(Ui.pressable(c, Ui.CARD, 16));
            TextView star = Ui.text(c, "✦", 16, 0xFF06111F, true);
            star.setGravity(Gravity.CENTER);
            star.setBackground(Ui.rounded(0xFFF2A93B, Ui.dp(c, 8)));
            row.addView(star, new LayoutParams(Ui.dp(c, 30), Ui.dp(c, 30)));
            TextView t = Ui.text(c, p.shortTitle, 16, Ui.TEXT, false);
            t.setPadding(Ui.dp(c, 14), 0, 0, 0);
            row.addView(t, new LayoutParams(0, -2, 1));
            row.addView(Ui.text(c, (prefs.getBoolean(p.master, true) ? "On" : "Off") + "   ›", 15, Ui.TEXT_DIM, false));
            row.setOnClickListener(v -> listener.openEnhancements(p));
            w.addView(row);
        }
        TextView wn = Ui.text(c, "Extra improvements made for specific games. They switch on by themselves when you play that game; tap one to see what it improves.", 13, Ui.TEXT_DIM, false);
        wn.setPadding(Ui.dp(c, 6), Ui.dp(c, 8), Ui.dp(c, 6), 0);
        list.addView(wn);

        // ---- controls
        LinearLayout ct = section("Controls");
        ct.addView(toggle("buttons", "On-screen buttons", "Turn off for full screen: just the DS screens and a small menu button. Also in the in-game menu."));
        ct.addView(Ui.divider(c));
        final float[] ops = {0.25f, 0.4f, 0.55f, 0.75f, 0.95f};
        int os = 2;
        for (int i = 0; i < ops.length; i++) if (Math.abs(ops[i] - opacity(prefs)) < 0.01f) os = i;
        ct.addView(label("Button opacity", null));
        ct.addView(pad(Ui.chips(c, new String[] {"25%", "40%", "55%", "75%", "95%"}, os, i -> {
            prefs.edit().putFloat("opacity", ops[i]).apply(); listener.settingChanged("opacity");
        })));
        ct.addView(Ui.divider(c));
        ct.addView(toggle("haptics", "Vibration", "A light tap when you press a button."));

        // ---- gameplay
        LinearLayout gp = section("Gameplay");
        gp.addView(toggle("resume", "Resume where you left off", "Leaving the app saves a resume point; opening the game continues from it."));
        gp.addView(Ui.divider(c));
        gp.addView(toggle("sound", "Sound", null));
        gp.addView(Ui.divider(c));
        final int[] ffs = {2, 3, 4};
        int fsel = 1;
        for (int i = 0; i < ffs.length; i++) if (ffs[i] == ffSpeed(prefs)) fsel = i;
        gp.addView(label("Fast forward speed", null));
        gp.addView(pad(Ui.chips(c, new String[] {"2x", "3x", "4x"}, fsel, i -> {
            prefs.edit().putInt("ffSpeed", ffs[i]).apply(); listener.settingChanged("ffSpeed");
        })));

        // ---- performance
        LinearLayout pf = section("Performance");
        pf.addView(toggle("jit", "Fast CPU mode (JIT)", "Much faster emulation. Turned off by itself if the app ever crashes with it. Applies the next time a game starts."));

        // ---- storage
        LinearLayout st = section("Library");
        st.addView(toggle("boxart", "Download box art", "Shows each game's real box art, downloaded once from GameTDB. Your own covers always come first."));
        st.addView(Ui.divider(c));
        st.addView(folderRow("Main games folder", folder, "Change", v -> listener.changeFolder()));
        for (String x : prefs.getString("extraDirs", "").split("\n")) {
            if (x.isEmpty()) continue;
            st.addView(Ui.divider(c));
            st.addView(folderRow("Games folder", x, "Remove", v -> listener.removeFolder(x)));
        }
        st.addView(Ui.divider(c));
        TextView addF = Ui.text(c, "+  Add another folder", 15, Ui.ACCENT, true);
        addF.setPadding(Ui.dp(c, 16), Ui.dp(c, 14), Ui.dp(c, 16), Ui.dp(c, 14));
        addF.setOnClickListener(v -> listener.addFolder());
        st.addView(addF);
        TextView note = Ui.text(c, "Games from every folder show up together in your library. Saves and save states are kept in a Saves folder next to each game, in the same format as the computer version.\n\n"
                + "Custom cover art: put a .png or .jpg with the same name as the game next to it (or in a Covers folder).", 13, Ui.TEXT_DIM, false);
        note.setPadding(Ui.dp(c, 6), Ui.dp(c, 10), Ui.dp(c, 6), 0);
        list.addView(note);
    }

    private View folderRow(String label, String path, String action, View.OnClickListener cb) {
        LinearLayout row = new LinearLayout(c);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(Ui.dp(c, 16), Ui.dp(c, 12), Ui.dp(c, 12), Ui.dp(c, 12));
        LinearLayout t = new LinearLayout(c);
        t.setOrientation(VERTICAL);
        t.addView(Ui.text(c, label, 15, Ui.TEXT, false));
        TextView p = Ui.text(c, path, 13, Ui.TEXT_DIM, false);
        p.setPadding(0, Ui.dp(c, 2), 0, 0);
        t.addView(p);
        row.addView(t, new LayoutParams(0, -2, 1));
        TextView a = Ui.text(c, action, 15, action.equals("Remove") ? 0xFFF87171 : Ui.ACCENT, true);
        a.setPadding(Ui.dp(c, 12), Ui.dp(c, 8), Ui.dp(c, 8), Ui.dp(c, 8));
        a.setOnClickListener(cb);
        row.addView(a);
        return row;
    }

    private LinearLayout section(String name) {
        TextView h = Ui.text(c, name.toUpperCase(), 12.5f, Ui.TEXT_DIM, true);
        h.setLetterSpacing(0.06f);
        h.setPadding(Ui.dp(c, 6), Ui.dp(c, 22), 0, Ui.dp(c, 8));
        list.addView(h);
        LinearLayout card = new LinearLayout(c);
        card.setOrientation(VERTICAL);
        card.setBackground(Ui.rounded(Ui.CARD, Ui.dp(c, 16)));
        list.addView(card);
        return card;
    }

    private View label(String title, String detail) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(VERTICAL);
        l.setPadding(Ui.dp(c, 16), Ui.dp(c, 14), Ui.dp(c, 16), Ui.dp(c, 4));
        l.addView(Ui.text(c, title, 16, Ui.TEXT, false));
        if (detail != null) {
            TextView d = Ui.text(c, detail, 13, Ui.TEXT_DIM, false);
            d.setPadding(0, Ui.dp(c, 3), 0, 0);
            l.addView(d);
        }
        return l;
    }

    private View pad(View v) {
        LinearLayout l = new LinearLayout(c);
        l.setPadding(Ui.dp(c, 16), Ui.dp(c, 8), Ui.dp(c, 16), Ui.dp(c, 14));
        l.addView(v, new LayoutParams(-1, -2));
        return l;
    }

    private View toggle(String key, String title, String detail) {
        LinearLayout row = new LinearLayout(c);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, 0, Ui.dp(c, 12), Ui.dp(c, 10));
        View text = label(title, detail);
        row.addView(text, new LayoutParams(0, -2, 1));
        Switch sw = new Switch(c);
        sw.setChecked(on(prefs, key));
        sw.setOnCheckedChangeListener((b, v) -> { prefs.edit().putBoolean(key, v).apply(); listener.settingChanged(key); });
        row.addView(sw);
        row.setOnClickListener(v -> sw.toggle());
        return row;
    }
}
