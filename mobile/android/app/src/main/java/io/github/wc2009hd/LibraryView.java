package io.github.wc2009hd;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AbsListView;
import android.widget.BaseAdapter;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** The game library: every .nds in the game folder as a tile, most recently played first. */
final class LibraryView extends LinearLayout {
    interface Listener {
        void play(Game g, boolean fresh);
        void gameOptions(Game g);
        void openSettings();
        void chooseFolder();
    }

    static final class Game {
        File rom;
        String title, publisher, base;
        Bitmap icon, cover;
        int tint = 0xFF2A3346;
        boolean resume, favorite, enhanced;
        long lastPlayed;
    }

    private final SharedPreferences prefs;
    private final Listener listener;
    private final List<Game> games = new ArrayList<>();
    private final GridView grid;
    private final View empty;
    private final Adapter adapter = new Adapter();
    private String dir = "";

    LibraryView(Context c, SharedPreferences prefs, Listener listener) {
        super(c);
        this.prefs = prefs; this.listener = listener;
        setOrientation(VERTICAL);
        setBackgroundColor(Ui.BG);

        // header
        LinearLayout bar = new LinearLayout(c);
        bar.setOrientation(HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(Ui.dp(c, 20), Ui.dp(c, 18), Ui.dp(c, 12), Ui.dp(c, 8));
        TextView title = Ui.text(c, "Library", 30, Ui.TEXT, true);
        bar.addView(title, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
        TextView gear = Ui.text(c, "Settings", 15, Ui.ACCENT, true);
        gear.setPadding(Ui.dp(c, 14), Ui.dp(c, 8), Ui.dp(c, 14), Ui.dp(c, 8));
        gear.setBackground(Ui.pressable(c, Ui.CARD, 18));
        gear.setOnClickListener(v -> listener.openSettings());
        bar.addView(gear);
        addView(bar);

        FrameLayout body = new FrameLayout(c);
        grid = new GridView(c);
        grid.setNumColumns(GridView.AUTO_FIT);
        grid.setColumnWidth(Ui.dp(c, 156));
        grid.setStretchMode(GridView.STRETCH_COLUMN_WIDTH);
        grid.setHorizontalSpacing(Ui.dp(c, 14));
        grid.setVerticalSpacing(Ui.dp(c, 16));
        grid.setPadding(Ui.dp(c, 16), Ui.dp(c, 8), Ui.dp(c, 16), Ui.dp(c, 24));
        grid.setClipToPadding(false);
        grid.setSelector(new android.graphics.drawable.ColorDrawable(0));
        grid.setAdapter(adapter);
        grid.setOnItemClickListener((p, v, pos, id) -> listener.play(games.get(pos), false));
        grid.setOnItemLongClickListener((p, v, pos, id) -> { listener.gameOptions(games.get(pos)); return true; });
        body.addView(grid);
        empty = emptyState(c);
        empty.setVisibility(GONE);
        body.addView(empty, new FrameLayout.LayoutParams(-1, -1));
        addView(body, new LayoutParams(-1, 0, 1));
    }

    private TextView folderLabel;

    /** Welcome screen when the folder has no games yet. */
    private View emptyState(Context c) {
        android.widget.ScrollView sv = new android.widget.ScrollView(c);
        LinearLayout col = new LinearLayout(c);
        col.setOrientation(VERTICAL);
        col.setGravity(Gravity.CENTER_HORIZONTAL);
        col.setPadding(Ui.dp(c, 28), Ui.dp(c, 30), Ui.dp(c, 28), Ui.dp(c, 30));
        sv.addView(col);

        ImageView art = new ImageView(c);
        art.setImageResource(R.mipmap.ic_launcher);
        col.addView(art, new LayoutParams(Ui.dp(c, 112), Ui.dp(c, 112)));

        TextView title = Ui.text(c, "Add your games", 28, Ui.TEXT, true);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, Ui.dp(c, 18), 0, Ui.dp(c, 6));
        col.addView(title, new LayoutParams(-1, -2));
        TextView sub = Ui.text(c, "Dual HD plays DS games (.nds files) with sharp HD upscaling, save states and controller support.", 16, Ui.TEXT_DIM, false);
        sub.setGravity(Gravity.CENTER);
        sub.setLineSpacing(0, 1.2f);
        col.addView(sub, new LayoutParams(-1, -2));

        LinearLayout card = new LinearLayout(c);
        card.setOrientation(VERTICAL);
        card.setPadding(Ui.dp(c, 18), Ui.dp(c, 16), Ui.dp(c, 18), Ui.dp(c, 16));
        card.setBackground(Ui.rounded(Ui.CARD, Ui.dp(c, 16)));
        card.addView(Ui.text(c, "Copy your .nds files into your games folder:", 15, Ui.TEXT, true));
        folderLabel = Ui.text(c, "", 14, Ui.ACCENT, false);
        folderLabel.setPadding(0, Ui.dp(c, 6), 0, Ui.dp(c, 6));
        card.addView(folderLabel);
        TextView hint = Ui.text(c, "Then come back here; they show up on their own. Some games get extra improvements, like Yu-Gi-Oh! 5D's WC2009 with HD cards (keep its HDCards folder next to the game).", 13.5f, Ui.TEXT_DIM, false);
        hint.setLineSpacing(0, 1.2f);
        card.addView(hint);
        LayoutParams lp = new LayoutParams(-1, -2);
        lp.topMargin = Ui.dp(c, 26);
        col.addView(card, lp);

        TextView choose = Ui.text(c, "Use another folder", 16, 0xFF06111F, true);
        choose.setGravity(Gravity.CENTER);
        choose.setPadding(0, Ui.dp(c, 15), 0, Ui.dp(c, 15));
        choose.setBackground(Ui.pressable(c, Ui.ACCENT, 14));
        choose.setOnClickListener(v -> listener.chooseFolder());
        LayoutParams bp = new LayoutParams(-1, -2);
        bp.topMargin = Ui.dp(c, 16);
        col.addView(choose, bp);
        return sv;
    }

    /** Rescans the folder (call after playing, to update the order and the resume badges). */
    void load(String folder) {
        dir = folder;
        final Handler ui = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            List<Game> found = scan(folder);
            ui.post(() -> {
                games.clear(); games.addAll(found);
                adapter.notifyDataSetChanged();
                folderLabel.setText(folder);
                empty.setVisibility(found.isEmpty() ? VISIBLE : GONE);
            });
        }).start();
    }

    private List<Game> scan(String folder) {
        List<Game> out = new ArrayList<>();
        File[] fs = new File(folder).listFiles();
        if (fs == null) return out;
        for (File f : fs) {
            String n = f.getName();
            if (!f.isFile() || n.startsWith(".") || !n.toLowerCase().endsWith(".nds")) continue;
            Game g = new Game();
            g.rom = f;
            g.base = n.substring(0, n.length() - 4);
            int[] px = new int[32 * 32];
            String info = Native.romInfo(f.getAbsolutePath(), px);
            String[] lines = info == null ? new String[0] : info.split("\n");
            if (lines.length >= 2) {
                g.title = TextUtils.join(" ", java.util.Arrays.copyOf(lines, lines.length - 1));
                g.publisher = lines[lines.length - 1];
            } else {
                g.title = lines.length == 1 && !lines[0].isEmpty() ? lines[0] : g.base;
                g.publisher = "";
            }
            g.title = prettyTitle(g.title);
            String custom = prefs.getString("name:" + n, "");
            if (!custom.isEmpty()) g.title = custom;
            g.favorite = prefs.getBoolean("fav:" + n, false);
            g.enhanced = Packs.forCode(Native.romCode(f.getAbsolutePath())) != null;
            g.icon = Bitmap.createBitmap(px, 32, 32, Bitmap.Config.ARGB_8888);
            g.tint = averageColor(px);
            g.cover = loadCover(folder, g.base);
            g.resume = new File(folder + "/Saves/" + g.base + ".ml0").exists();
            g.lastPlayed = prefs.getLong("played:" + n, 0);
            out.add(g);
        }
        Collections.sort(out, (a, b) -> a.favorite != b.favorite ? (a.favorite ? -1 : 1) : a.lastPlayed != b.lastPlayed ? Long.compare(b.lastPlayed, a.lastPlayed)
                : a.title.compareToIgnoreCase(b.title));
        return out;
    }

    /** "STARDUST ACCELERATOR" -> "Stardust Accelerator" (banner titles are often in capitals) */
    private static String prettyTitle(String s) {
        StringBuilder b = new StringBuilder();
        for (String w : s.split(" ")) {
            if (b.length() > 0) b.append(' ');
            boolean caps = w.length() > 3 && w.equals(w.toUpperCase()) && !w.equals(w.toLowerCase());
            b.append(caps ? w.charAt(0) + w.substring(1).toLowerCase() : w);
        }
        return b.toString();
    }

    private static Bitmap loadCover(String folder, String base) {
        for (String d : new String[] {folder + "/Covers", folder})
            for (String ext : new String[] {".png", ".jpg", ".jpeg", ".webp"}) {
                File f = new File(d, base + ext);
                if (f.exists()) {
                    BitmapFactory.Options o = new BitmapFactory.Options();
                    o.inJustDecodeBounds = true;
                    BitmapFactory.decodeFile(f.getAbsolutePath(), o);
                    o.inJustDecodeBounds = false;
                    o.inSampleSize = 1;
                    while (Math.min(o.outWidth, o.outHeight) / (o.inSampleSize * 2) >= 512) o.inSampleSize *= 2;
                    Bitmap bm = BitmapFactory.decodeFile(f.getAbsolutePath(), o);
                    if (bm != null) return bm;
                }
            }
        return null;
    }

    private static int averageColor(int[] px) {
        long r = 0, g = 0, b = 0, n = 0;
        for (int p : px) {
            if ((p >>> 24) == 0) continue;
            r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
        }
        if (n == 0) return 0xFF2A3346;
        return 0xFF000000 | (int) (r / n) << 16 | (int) (g / n) << 8 | (int) (b / n);
    }

    // ---- tiles ----

    private final class Adapter extends BaseAdapter {
        @Override public int getCount() { return games.size(); }
        @Override public Object getItem(int i) { return games.get(i); }
        @Override public long getItemId(int i) { return i; }

        @Override
        public View getView(int pos, View convert, ViewGroup parent) {
            Tile t = convert instanceof Tile ? (Tile) convert : new Tile(getContext());
            t.bind(games.get(pos));
            return t;
        }
    }

    private static final class Tile extends LinearLayout {
        final Art art;
        final TextView title, sub;

        Tile(Context c) {
            super(c);
            setOrientation(VERTICAL);
            setBackground(Ui.pressable(c, Ui.CARD, 16));
            setLayoutParams(new AbsListView.LayoutParams(-1, -2));
            art = new Art(c);
            addView(art, new LayoutParams(-1, -2));
            title = Ui.text(c, "", 15, Ui.TEXT, true);
            title.setMaxLines(2);
            title.setEllipsize(TextUtils.TruncateAt.END);
            title.setPadding(Ui.dp(c, 12), Ui.dp(c, 10), Ui.dp(c, 12), 0);
            addView(title);
            sub = Ui.text(c, "", 12.5f, Ui.TEXT_DIM, false);
            sub.setSingleLine(true);
            sub.setEllipsize(TextUtils.TruncateAt.END);
            sub.setPadding(Ui.dp(c, 12), Ui.dp(c, 2), Ui.dp(c, 12), Ui.dp(c, 12));
            addView(sub);
        }

        void bind(Game g) {
            art.game = g;
            art.invalidate();
            title.setText(g.title);
            sub.setText(g.publisher);
        }
    }

    /** Square cover: the custom picture, or the game's icon (crisp pixels) on a tinted gradient. */
    private static final class Art extends View {
        Game game;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint pix = new Paint();
        private final Path clip = new Path();
        private final float r;

        Art(Context c) { super(c); r = Ui.dp(c, 16); pix.setFilterBitmap(false); }

        @Override
        protected void onMeasure(int w, int h) {
            int size = MeasureSpec.getSize(w);
            setMeasuredDimension(size, size);
        }

        @Override
        protected void onDraw(Canvas c) {
            if (game == null) return;
            int w = getWidth(), h = getHeight();
            clip.reset();
            clip.addRoundRect(new RectF(0, 0, w, h + r), new float[] {r, r, r, r, 0, 0, 0, 0}, Path.Direction.CW);
            c.save();
            c.clipPath(clip);
            if (game.cover != null) {
                Bitmap b = game.cover;
                float s = Math.max((float) w / b.getWidth(), (float) h / b.getHeight());
                float bw = b.getWidth() * s, bh = b.getHeight() * s;
                paint.setFilterBitmap(true);
                c.drawBitmap(b, null, new RectF((w - bw) / 2, (h - bh) / 2, (w + bw) / 2, (h + bh) / 2), paint);
            } else {
                int t = game.tint;
                paint.setShader(new LinearGradient(0, 0, w, h, mix(t, 0xFF0A0E16, 0.45f), mix(t, 0xFF0A0E16, 0.8f), Shader.TileMode.CLAMP));
                c.drawRect(0, 0, w, h, paint);
                paint.setShader(null);
                int s = (int) (w * 0.56f) / 32 * 32;   // whole multiple of 32: even pixels
                if (s < 32) s = (int) (w * 0.56f);
                int x = (w - s) / 2, y = (h - s) / 2;
                paint.setColor(0x55000000);
                c.drawRoundRect(new RectF(x + s * 0.03f, y + s * 0.06f, x + s * 1.03f, y + s * 1.06f), s * 0.08f, s * 0.08f, paint);
                c.drawBitmap(game.icon, new Rect(0, 0, 32, 32), new Rect(x, y, x + s, y + s), pix);
            }
            c.restore();
            float bh = Ui.dp(getContext(), 22), bm = Ui.dp(getContext(), 8);
            paint.setTextSize(Ui.dp(getContext(), 11));
            paint.setFakeBoldText(true);
            paint.setTextAlign(Paint.Align.CENTER);
            if (game.favorite) {
                RectF sr = new RectF(bm, bm, bm + bh, bm + bh);
                paint.setColor(0xD90A0E16);
                c.drawRoundRect(sr, bh / 2, bh / 2, paint);
                paint.setColor(0xFFFACC15);
                paint.setTextSize(Ui.dp(getContext(), 13));
                c.drawText("★", sr.centerX(), sr.centerY() - (paint.descent() + paint.ascent()) / 2, paint);
                paint.setTextSize(Ui.dp(getContext(), 11));
            }
            if (game.enhanced) {
                float bw = Ui.dp(getContext(), 42);
                RectF hr = new RectF(bm, h - bm - bh, bm + bw, h - bm);
                paint.setColor(0xD90A0E16);
                c.drawRoundRect(hr, bh / 2, bh / 2, paint);
                paint.setColor(0xFFF2C14E);
                c.drawText("✦ HD", hr.centerX(), hr.centerY() - (paint.descent() + paint.ascent()) / 2, paint);
            }
            if (game.resume) {
                paint.setColor(0xE60A0E16);
                float ph = Ui.dp(getContext(), 22), pw = Ui.dp(getContext(), 64), m = Ui.dp(getContext(), 8);
                RectF pr = new RectF(w - m - pw, m, w - m, m + ph);
                c.drawRoundRect(pr, ph / 2, ph / 2, paint);
                paint.setColor(Ui.ACCENT);
                paint.setTextSize(Ui.dp(getContext(), 11));
                paint.setFakeBoldText(true);
                paint.setTextAlign(Paint.Align.CENTER);
                c.drawText("RESUME", pr.centerX(), pr.centerY() - (paint.descent() + paint.ascent()) / 2, paint);
            }
        }

        private static int mix(int a, int b, float t) {
            int r = (int) (((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
            int g = (int) (((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
            int bl = (int) ((a & 255) * (1 - t) + (b & 255) * t);
            return 0xFF000000 | r << 16 | g << 8 | bl;
        }
    }
}
