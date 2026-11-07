package io.github.wc2009hd;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.content.res.ColorStateList;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Shared look: dark background, cool blue accent, rounded cards. */
final class Ui {
    static final int BG = 0xFF0A0E16, CARD = 0xFF151B27, CARD_HI = 0xFF1D2534, LINE = 0xFF263042;
    static final int TEXT = 0xFFF2F5FA, TEXT_DIM = 0xFF9AA6B8, ACCENT = 0xFF4AA3FF, ACCENT_DARK = 0xFF1E5FAF;

    static int dp(Context c, float v) { return Math.round(v * c.getResources().getDisplayMetrics().density); }

    static GradientDrawable rounded(int color, float radiusPx) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(radiusPx);
        return d;
    }

    static Drawable pressable(Context c, int color, float radiusDp) {
        GradientDrawable base = rounded(color, dp(c, radiusDp));
        GradientDrawable mask = rounded(Color.WHITE, dp(c, radiusDp));
        return new RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), base, mask);
    }

    static TextView text(Context c, String s, float sizeSp, int color, boolean bold) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(sizeSp);
        t.setTextColor(color);
        t.setTypeface(Typeface.create("sans-serif" + (bold ? "-medium" : ""), Typeface.NORMAL));
        if (bold) t.setTypeface(t.getTypeface(), Typeface.BOLD);
        return t;
    }

    /** Pill-shaped choice buttons in a row; returns the row. */
    interface Choice { void picked(int index); }

    static LinearLayout chips(Context c, String[] labels, int selected, Choice cb) {
        LinearLayout row = new LinearLayout(c);
        row.setOrientation(LinearLayout.HORIZONTAL);
        TextView[] views = new TextView[labels.length];
        for (int i = 0; i < labels.length; i++) {
            final int idx = i;
            TextView t = text(c, labels[i], 14, TEXT, true);
            t.setGravity(Gravity.CENTER);
            t.setPadding(dp(c, 8), dp(c, 9), dp(c, 8), dp(c, 9));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
            if (i > 0) lp.leftMargin = dp(c, 6);
            t.setLayoutParams(lp);
            views[i] = t;
            t.setOnClickListener(v -> {
                for (int k = 0; k < views.length; k++) styleChip(c, views[k], k == idx);
                cb.picked(idx);
            });
            styleChip(c, t, i == selected);
            row.addView(t);
        }
        return row;
    }

    static void styleChip(Context c, TextView t, boolean on) {
        t.setBackground(pressable(c, on ? ACCENT : CARD_HI, 10));
        t.setTextColor(on ? 0xFF06111F : TEXT);
    }

    static View divider(Context c) {
        View v = new View(c);
        v.setBackgroundColor(LINE);
        v.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Math.max(1, dp(c, 0.7f))));
        return v;
    }

    private Ui() {}
}
