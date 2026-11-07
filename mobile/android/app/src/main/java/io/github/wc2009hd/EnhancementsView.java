package io.github.wc2009hd;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/** One game's enhancement pack: the master switch and its details. */
final class EnhancementsView extends LinearLayout {
    interface Listener { void settingChanged(String key); void closeEnhancements(); }

    private final List<Switch> details = new ArrayList<>();
    private final List<View> detailRows = new ArrayList<>();

    EnhancementsView(Context c, SharedPreferences prefs, Packs.Pack pack, Listener listener, String gameDir) {
        super(c);
        setOrientation(VERTICAL);
        setBackgroundColor(Ui.BG);
        setClickable(true);

        LinearLayout bar = new LinearLayout(c);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(Ui.dp(c, 8), Ui.dp(c, 14), Ui.dp(c, 20), Ui.dp(c, 6));
        TextView back = Ui.text(c, "‹  Settings", 16, Ui.ACCENT, true);
        back.setPadding(Ui.dp(c, 12), Ui.dp(c, 8), Ui.dp(c, 14), Ui.dp(c, 8));
        back.setBackground(Ui.pressable(c, Ui.BG, 18));
        back.setOnClickListener(v -> listener.closeEnhancements());
        bar.addView(back);
        addView(bar);

        ScrollView sv = new ScrollView(c);
        LinearLayout list = new LinearLayout(c);
        list.setOrientation(VERTICAL);
        list.setPadding(Ui.dp(c, 16), Ui.dp(c, 4), Ui.dp(c, 16), Ui.dp(c, 32));
        sv.addView(list);
        addView(sv, new LayoutParams(-1, 0, 1));

        TextView title = Ui.text(c, pack.title, 26, Ui.TEXT, true);
        title.setPadding(Ui.dp(c, 6), Ui.dp(c, 6), Ui.dp(c, 6), Ui.dp(c, 6));
        list.addView(title);
        TextView about = Ui.text(c, pack.about, 14, Ui.TEXT_DIM, false);
        about.setLineSpacing(0, 1.2f);
        about.setPadding(Ui.dp(c, 6), 0, Ui.dp(c, 6), Ui.dp(c, 18));
        list.addView(about);

        LinearLayout card = card(c, list);
        boolean on = prefs.getBoolean(pack.master, true);
        card.addView(row(c, prefs, pack.master, "Enhancements",
                "Off plays the game exactly like the original DS. You can also switch it from the in-game menu to compare.",
                true, (key, v) -> {
                    listener.settingChanged(key);
                    for (Switch s : details) s.setEnabled(v);
                    for (View r : detailRows) r.setAlpha(v ? 1f : 0.45f);
                }));

        TextView wh = Ui.text(c, "WHAT'S IMPROVED", 12.5f, Ui.TEXT_DIM, true);
        wh.setLetterSpacing(0.06f);
        wh.setPadding(Ui.dp(c, 6), Ui.dp(c, 22), 0, Ui.dp(c, 8));
        list.addView(wh);
        LinearLayout fc = card(c, list);
        fc.setPadding(Ui.dp(c, 16), Ui.dp(c, 8), Ui.dp(c, 16), Ui.dp(c, 8));
        for (String f : pack.features) {
            LinearLayout fr = new LinearLayout(c);
            fr.setPadding(0, Ui.dp(c, 8), 0, Ui.dp(c, 8));
            TextView ck = Ui.text(c, "✓", 15, 0xFF22C55E, true);
            ck.setPadding(0, 0, Ui.dp(c, 12), 0);
            fr.addView(ck);
            fr.addView(Ui.text(c, f, 15, Ui.TEXT, false), new LayoutParams(0, -2, 1));
            fc.addView(fr);
        }
        // what you need, where it goes, and whether it's there
        TextView nh = Ui.text(c, "WHAT YOU NEED: HD PACK (" + pack.needs.toUpperCase() + " FOLDER)", 12.5f, Ui.TEXT_DIM, true);
        nh.setLetterSpacing(0.06f);
        nh.setPadding(Ui.dp(c, 6), Ui.dp(c, 22), 0, Ui.dp(c, 8));
        list.addView(nh);
        LinearLayout sc = card(c, list);
        sc.setPadding(Ui.dp(c, 16), Ui.dp(c, 12), Ui.dp(c, 16), Ui.dp(c, 12));
        java.io.File[] files = new java.io.File(gameDir).listFiles();
        int found = 0;
        if (files != null)
            for (java.io.File f : files) {
                if (!f.isFile() || !f.getName().toLowerCase().endsWith(".nds")) continue;
                String code = Native.romCode(f.getAbsolutePath());
                if (code == null || !code.startsWith(pack.code)) continue;
                found++;
                boolean ok = new java.io.File(gameDir, pack.needs).isDirectory();
                LinearLayout r = new LinearLayout(c);
                r.setPadding(0, Ui.dp(c, 6), 0, Ui.dp(c, 6));
                TextView ic = Ui.text(c, ok ? "✓" : "!", 15, 0xFF06111F, true);
                ic.setGravity(Gravity.CENTER);
                ic.setBackground(Ui.rounded(ok ? 0xFF22C55E : 0xFFF59E0B, Ui.dp(c, 8)));
                r.addView(ic, new LayoutParams(Ui.dp(c, 28), Ui.dp(c, 28)));
                LinearLayout t = new LinearLayout(c);
                t.setOrientation(VERTICAL);
                t.setPadding(Ui.dp(c, 14), 0, 0, 0);
                t.addView(Ui.text(c, ok ? "Ready" : "HD pack missing", 16, Ui.TEXT, true));
                t.addView(Ui.text(c, pack.needs + (ok ? " found" : " not found") + " next to " + f.getName(), 13, Ui.TEXT_DIM, false));
                r.addView(t, new LayoutParams(0, -2, 1));
                sc.addView(r);
            }
        if (found == 0) sc.addView(Ui.text(c, "Game not in your games folder yet", 15, Ui.TEXT_DIM, false));
        TextView nd = Ui.text(c, pack.needsDetail + "\n\nPut it in the same folder as the game, like this:\n\n   📁 Your games folder\n        🎮 " + pack.shortTitle
                + ".nds\n        📁 " + pack.needs + "\n\nKeep the folder name exactly \"" + pack.needs + "\". Without it the game runs like the original.", 13, Ui.TEXT_DIM, false);
        nd.setLineSpacing(0, 1.2f);
        nd.setPadding(Ui.dp(c, 6), Ui.dp(c, 10), Ui.dp(c, 6), 0);
        list.addView(nd);

        TextView h = Ui.text(c, "FINE-TUNE", 12.5f, Ui.TEXT_DIM, true);
        h.setLetterSpacing(0.06f);
        h.setPadding(Ui.dp(c, 6), Ui.dp(c, 22), 0, Ui.dp(c, 8));
        list.addView(h);
        LinearLayout d = card(c, list);
        for (int i = 0; i < pack.options.length; i++) {
            Packs.Option o = pack.options[i];
            if (i > 0) d.addView(Ui.divider(c));
            View r = row(c, prefs, o.key, o.title, o.detail, false, (key, v) -> listener.settingChanged(key));
            r.setAlpha(on ? 1f : 0.45f);
            detailRows.add(r);
            d.addView(r);
        }
        for (Switch s : details) s.setEnabled(on);
    }

    private interface Toggled { void changed(String key, boolean value); }

    private static LinearLayout card(Context c, LinearLayout list) {
        LinearLayout card = new LinearLayout(c);
        card.setOrientation(VERTICAL);
        card.setBackground(Ui.rounded(Ui.CARD, Ui.dp(c, 16)));
        list.addView(card);
        return card;
    }

    private View row(Context c, SharedPreferences prefs, String key, String title, String detail, boolean master, Toggled cb) {
        LinearLayout row = new LinearLayout(c);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(Ui.dp(c, 16), Ui.dp(c, 14), Ui.dp(c, 12), Ui.dp(c, 14));
        LinearLayout t = new LinearLayout(c);
        t.setOrientation(VERTICAL);
        t.addView(Ui.text(c, title, 16, Ui.TEXT, master));
        TextView dt = Ui.text(c, detail, 13, Ui.TEXT_DIM, false);
        dt.setPadding(0, Ui.dp(c, 3), 0, 0);
        t.addView(dt);
        row.addView(t, new LayoutParams(0, -2, 1));
        Switch sw = new Switch(c);
        sw.setChecked(prefs.getBoolean(key, true));
        sw.setOnCheckedChangeListener((b, v) -> { prefs.edit().putBoolean(key, v).apply(); cb.changed(key, v); });
        row.addView(sw);
        row.setOnClickListener(v -> { if (sw.isEnabled()) sw.toggle(); });
        if (!master) details.add(sw);
        return row;
    }
}
