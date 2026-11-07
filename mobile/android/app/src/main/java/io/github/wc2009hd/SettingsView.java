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
    }

    // keys and defaults
    static int scale(SharedPreferences p) { return p.getInt("scale", 4); }
    static boolean on(SharedPreferences p, String key) {
        switch (key) {
            case "jit": case "filter": case "smooth2d": case "hdCards": case "hdText": case "buttons":
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
        LinearLayout g = section("Graphics");
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

        // ---- WC2009
        LinearLayout w = section("Yu-Gi-Oh! 5D's WC2009 HD");
        w.addView(toggle("hdCards", "HD card art", "Real card images instead of the DS's small pictures (needs the HDCards folder)."));
        w.addView(Ui.divider(c));
        w.addView(toggle("hdText", "HD text and menus", "Card text, descriptions and panels redrawn in a clean font."));

        // ---- controls
        LinearLayout ct = section("Controls");
        ct.addView(toggle("buttons", "On-screen buttons", "Turn off to play only with touch or a controller."));
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
        LinearLayout st = section("Games folder");
        TextView path = Ui.text(c, folder, 14, Ui.TEXT_DIM, false);
        path.setPadding(Ui.dp(c, 16), Ui.dp(c, 14), Ui.dp(c, 16), Ui.dp(c, 6));
        st.addView(path);
        TextView change = Ui.text(c, "Change folder", 15, Ui.ACCENT, true);
        change.setPadding(Ui.dp(c, 16), Ui.dp(c, 8), Ui.dp(c, 16), Ui.dp(c, 14));
        change.setOnClickListener(v -> listener.changeFolder());
        st.addView(change);
        TextView note = Ui.text(c, "Saves and save states are kept in the Saves folder inside it, in the same format as the computer version.\n\n"
                + "Custom cover art: put a .png or .jpg with the same name as the game next to it (or in a Covers folder).", 13, Ui.TEXT_DIM, false);
        note.setPadding(Ui.dp(c, 6), Ui.dp(c, 10), Ui.dp(c, 6), 0);
        list.addView(note);
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
