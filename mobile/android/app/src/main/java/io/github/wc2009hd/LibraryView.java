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
        void importGames();
        void sortMenu();
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
        LinearLayout titles = new LinearLayout(c);
        titles.setOrientation(VERTICAL);
        TextView brand = Ui.text(c, "DUAL HD", 12, Ui.ACCENT, true);
        brand.setLetterSpacing(0.22f);
        titles.addView(brand);
        TextView title = Ui.text(c, "Nintendo DS", 28, Ui.TEXT, true);
        titles.addView(title);
        bar.addView(titles, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
        TextView sortB = Ui.text(c, "⇅", 17, Ui.ACCENT, true);
        sortB.setPadding(Ui.dp(c, 14), Ui.dp(c, 6), Ui.dp(c, 14), Ui.dp(c, 6));
        sortB.setBackground(Ui.pressable(c, Ui.CARD, 18));
        sortB.setOnClickListener(v -> listener.sortMenu());
        LayoutParams sp = new LayoutParams(-2, -2);
        sp.rightMargin = Ui.dp(c, 8);
        bar.addView(sortB, sp);
        TextView add = Ui.text(c, "+  Add", 15, Ui.ACCENT, true);
        add.setPadding(Ui.dp(c, 14), Ui.dp(c, 8), Ui.dp(c, 14), Ui.dp(c, 8));
        add.setBackground(Ui.pressable(c, Ui.CARD, 18));
        add.setOnClickListener(v -> listener.importGames());
        LayoutParams ap = new LayoutParams(-2, -2);
        ap.rightMargin = Ui.dp(c, 8);
        bar.addView(add, ap);
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

        TextView brand = Ui.text(c, "DUAL HD", 13, Ui.ACCENT, true);
        brand.setLetterSpacing(0.27f);
        brand.setGravity(Gravity.CENTER);
        brand.setPadding(0, Ui.dp(c, 16), 0, 0);
        col.addView(brand, new LayoutParams(-1, -2));
        TextView title = Ui.text(c, "Your DS games, in HD", 28, Ui.TEXT, true);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, Ui.dp(c, 6), 0, Ui.dp(c, 6));
        col.addView(title, new LayoutParams(-1, -2));
        TextView sub = Ui.text(c, "Add your games to get started. Pick the folder that has them, or import .nds files one by one.", 16, Ui.TEXT_DIM, false);
        sub.setGravity(Gravity.CENTER);
        sub.setLineSpacing(0, 1.2f);
        col.addView(sub, new LayoutParams(-1, -2));

        TextView choose = Ui.text(c, "Choose games folder", 16, 0xFF06111F, true);
        choose.setGravity(Gravity.CENTER);
        choose.setPadding(0, Ui.dp(c, 15), 0, Ui.dp(c, 15));
        choose.setBackground(Ui.pressable(c, Ui.ACCENT, 14));
        choose.setOnClickListener(v -> listener.chooseFolder());
        LayoutParams bp = new LayoutParams(-1, -2);
        bp.topMargin = Ui.dp(c, 26);
        col.addView(choose, bp);
        TextView imp = Ui.text(c, "Import games", 16, Ui.ACCENT, true);
        imp.setGravity(Gravity.CENTER);
        imp.setPadding(0, Ui.dp(c, 15), 0, Ui.dp(c, 15));
        imp.setBackground(Ui.pressable(c, 0xFF1A2B45, 14));
        imp.setOnClickListener(v -> listener.importGames());
        LayoutParams ip = new LayoutParams(-1, -2);
        ip.topMargin = Ui.dp(c, 12);
        col.addView(imp, ip);

        folderLabel = Ui.text(c, "", 13, Ui.TEXT_DIM, false);
        folderLabel.setGravity(Gravity.CENTER);
        folderLabel.setPadding(0, Ui.dp(c, 10), 0, 0);
        col.addView(folderLabel, new LayoutParams(-1, -2));

        TextView why = Ui.text(c, "WHY DUAL HD", 12.5f, Ui.TEXT_DIM, true);
        why.setLetterSpacing(0.06f);
        LayoutParams wl = new LayoutParams(-1, -2);
        wl.topMargin = Ui.dp(c, 30);
        col.addView(why, wl);
        col.addView(tip(c, "HD", "Up to 8x resolution", "Sharp HD upscaling for every game, from 2x to 8x, with smooth 2D graphics."), tipLp(c, 8));
        col.addView(tip(c, "▤", "Made for big screens", "Layouts for portrait, landscape and foldables, full screen mode and game controllers."), tipLp(c, 12));
        col.addView(tip(c, "↺", "Never lose progress", "Save states, and every game picks up right where you left off."), tipLp(c, 12));
        col.addView(tip(c, "✦", "Game enhancements", "Some games get extra improvements made just for them. See Settings > Game enhancements."), tipLp(c, 12));
        return sv;
    }

    private static LayoutParams tipLp(Context c, int topDp) {
        LayoutParams lp = new LayoutParams(-1, -2);
        lp.topMargin = Ui.dp(c, topDp);
        return lp;
    }

    private static View tip(Context c, String icon, String title, String text) {
        LinearLayout card = new LinearLayout(c);
        card.setPadding(Ui.dp(c, 16), Ui.dp(c, 14), Ui.dp(c, 16), Ui.dp(c, 14));
        card.setBackground(Ui.rounded(Ui.CARD, Ui.dp(c, 16)));
        TextView ic = Ui.text(c, icon, 15, 0xFF06111F, true);
        ic.setGravity(Gravity.CENTER);
        ic.setBackground(Ui.rounded(Ui.ACCENT, Ui.dp(c, 8)));
        card.addView(ic, new LayoutParams(Ui.dp(c, 30), Ui.dp(c, 30)));
        LinearLayout t = new LinearLayout(c);
        t.setOrientation(VERTICAL);
        t.setPadding(Ui.dp(c, 14), 0, 0, 0);
        t.addView(Ui.text(c, title, 15, Ui.TEXT, true));
        TextView b = Ui.text(c, text, 13.5f, Ui.TEXT_DIM, false);
        b.setLineSpacing(0, 1.2f);
        b.setPadding(0, Ui.dp(c, 3), 0, 0);
        t.addView(b);
        card.addView(t, new LayoutParams(0, -2, 1));
        return card;
    }

    private static final java.util.Set<String> fetching = java.util.Collections.synchronizedSet(new java.util.HashSet<>());

    /** Box art from GameTDB by the game's 4-letter code (tried again after a week if not found). */
    private void fetchBoxArt(String code, File out) {
        long missed = prefs.getLong("boxartMiss:" + code, 0);
        if (missed > 0 && System.currentTimeMillis() - missed < 7L * 86400 * 1000) return;
        if (!fetching.add(code)) return;
        String region;
        switch (code.charAt(3)) {
            case 'E': region = "US"; break; case 'J': region = "JA"; break; case 'D': region = "DE"; break;
            case 'F': region = "FR"; break; case 'S': region = "ES"; break; case 'I': region = "IT"; break;
            case 'K': region = "KO"; break; case 'U': region = "AU"; break; case 'H': region = "NL"; break;
            default: region = "EN";
        }
        java.util.LinkedHashSet<String> regions = new java.util.LinkedHashSet<>(java.util.Arrays.asList(region, "US", "EN", "JA"));
        new Thread(() -> {
            for (String r : regions)
                for (String kind : new String[] {"coverHQ/%s/%s.jpg", "coverM/%s/%s.jpg", "cover/%s/%s.jpg", "coverS/%s/%s.png"}) {
                    try {
                        java.net.HttpURLConnection con = (java.net.HttpURLConnection) new java.net.URL("https://art.gametdb.com/ds/" + String.format(kind, r, code)).openConnection();
                        con.setConnectTimeout(8000); con.setReadTimeout(15000);
                        if (con.getResponseCode() != 200) { con.disconnect(); continue; }
                        Bitmap bm;
                        try (java.io.InputStream in = con.getInputStream()) { bm = BitmapFactory.decodeStream(in); }
                        con.disconnect();
                        if (bm == null || bm.getWidth() < 64) continue;
                        out.getParentFile().mkdirs();
                        try (java.io.FileOutputStream os = new java.io.FileOutputStream(out)) { bm.compress(Bitmap.CompressFormat.JPEG, 92, os); }
                        fetching.remove(code);
                        post(() -> { removeCallbacks(reloadSoon); postDelayed(reloadSoon, 300); });
                        return;
                    } catch (Exception ignored) {}
                }
            prefs.edit().putLong("boxartMiss:" + code, System.currentTimeMillis()).apply();
            fetching.remove(code);
        }).start();
    }

    private void sortGames(List<Game> out) {
        String sort = prefs.getString("sort", "recent");
        java.util.List<String> order = java.util.Arrays.asList(prefs.getString("order", "").split("\n"));
        Collections.sort(out, (a, b) -> a.favorite != b.favorite ? (a.favorite ? -1 : 1)
                : sort.equals("name") ? a.title.compareToIgnoreCase(b.title)
                : sort.equals("custom") ? (rank(order, a) != rank(order, b) ? Integer.compare(rank(order, a), rank(order, b)) : a.title.compareToIgnoreCase(b.title))
                : a.lastPlayed != b.lastPlayed ? Long.compare(b.lastPlayed, a.lastPlayed)
                : a.title.compareToIgnoreCase(b.title));
    }

    private static int rank(java.util.List<String> order, Game g) {
        int i = order.indexOf(g.rom.getName());
        return i < 0 ? Integer.MAX_VALUE : i;
    }

    /** The games as shown, for moving one in the custom order. */
    java.util.List<Game> shown() { return new ArrayList<>(games); }

    /** Full name and publisher from GameTDB, cached as "tdb:<code>" = "title\npublisher". */
    private void fetchInfo(String code) {
        long missed = prefs.getLong("tdbMiss:" + code, 0);
        if (missed > 0 && System.currentTimeMillis() - missed < 7L * 86400 * 1000) return;
        if (!fetching.add("i" + code)) return;
        new Thread(() -> {
            String title = null, pub = null;
            try {
                java.net.HttpURLConnection con = (java.net.HttpURLConnection) new java.net.URL("https://www.gametdb.com/DS/" + code).openConnection();
                con.setConnectTimeout(8000); con.setReadTimeout(15000);
                if (con.getResponseCode() == 200) {
                    StringBuilder sb = new StringBuilder();
                    try (java.io.BufferedReader rd = new java.io.BufferedReader(new java.io.InputStreamReader(con.getInputStream(), "UTF-8"))) {
                        String line;
                        while ((line = rd.readLine()) != null) sb.append(line).append('\n');
                    }
                    String html = sb.toString();
                    title = match(html, "title \\(EN\\)\\s*</t[dh]>\\s*<td[^>]*>(.*?)</td>");
                    if (title == null) {
                        String t = match(html, "<title>(.*?)</title>");
                        if (t != null) {
                            int d = t.indexOf(" - "); if (d >= 0) t = t.substring(d + 3);
                            int bar = t.indexOf(" | "); if (bar >= 0) t = t.substring(0, bar);
                            if (!t.toLowerCase().contains("gametdb") && !t.isEmpty()) title = t;
                        }
                    }
                    pub = match(html, "publisher\\s*</t[dh]>\\s*<td[^>]*>(.*?)</td>");
                }
                con.disconnect();
            } catch (Exception ignored) {}
            if (title != null && !title.isEmpty()) {
                prefs.edit().putString("tdb:" + code, title + "\n" + (pub == null ? "" : pub)).apply();
                post(() -> { removeCallbacks(reloadSoon); postDelayed(reloadSoon, 300); });
            } else prefs.edit().putLong("tdbMiss:" + code, System.currentTimeMillis()).apply();
            fetching.remove("i" + code);
        }).start();
    }

    private static String match(String html, String re) {
        java.util.regex.Matcher m = java.util.regex.Pattern.compile(re, java.util.regex.Pattern.CASE_INSENSITIVE | java.util.regex.Pattern.DOTALL).matcher(html);
        if (!m.find()) return null;
        String s = m.group(1).replaceAll("<[^>]+>", "").replace("&amp;", "&").replace("&#039;", "'").replace("&#39;", "'")
                .replace("&quot;", "\"").replace("&nbsp;", " ").replace("&eacute;", "é").trim();
        return s.isEmpty() ? null : s;
    }

    private final Runnable reloadSoon = () -> { if (!dir.isEmpty()) load(dir); };

    /** Rescans the folder (call after playing, to update the order and the resume badges). */
    void load(String folder) {
        dir = folder;
        final Handler ui = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            // the main games folder, plus any extra folders added in Settings
            List<Game> found = new ArrayList<>();
            java.util.LinkedHashSet<String> dirs = new java.util.LinkedHashSet<>();
            dirs.add(folder);
            for (String x : prefs.getString("extraDirs", "").split("\n")) if (!x.isEmpty()) dirs.add(x);
            for (String d : dirs) found.addAll(scan(d));
            sortGames(found);
            ui.post(() -> {
                games.clear(); games.addAll(found);
                adapter.notifyDataSetChanged();
                folderLabel.setText("Games folder: " + folder);
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
            String codeT = Native.romCode(f.getAbsolutePath());
            String tdb = prefs.getString("tdb:" + codeT, "");
            if (!tdb.isEmpty()) {
                String[] tp = tdb.split("\n", -1);
                g.title = tp[0];
                if (tp.length > 1 && !tp[1].isEmpty()) g.publisher = tp[1];
            } else if (codeT != null && codeT.length() == 4 && prefs.getBoolean("boxart", true)) fetchInfo(codeT);
            String custom = prefs.getString("name:" + n, "");
            if (!custom.isEmpty()) g.title = custom;
            g.favorite = prefs.getBoolean("fav:" + n, false);
            g.enhanced = Packs.forCode(Native.romCode(f.getAbsolutePath())) != null;
            g.icon = Bitmap.createBitmap(px, 32, 32, Bitmap.Config.ARGB_8888);
            g.tint = averageColor(px);
            g.cover = loadCover(folder, g.base);
            String code = Native.romCode(f.getAbsolutePath());
            if (g.cover == null && code != null && code.length() == 4) {
                File art = new File(getContext().getFilesDir(), "boxart/" + code + ".jpg");
                if (art.exists()) g.cover = BitmapFactory.decodeFile(art.getAbsolutePath());
                else if (prefs.getBoolean("boxart", true)) fetchBoxArt(code, art);
            }
            g.resume = new File(folder + "/Saves/" + g.base + ".ml0").exists();
            g.lastPlayed = prefs.getLong("played:" + n, 0);
            out.add(g);
        }
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
            setBackground(Ui.pressable(c, 0x00000000, 16));
            setLayoutParams(new AbsListView.LayoutParams(-1, -2));
            art = new Art(c);
            addView(art, new LayoutParams(-1, -2));
            // the whole name, however many lines it takes (the grid row grows to fit)
            title = Ui.text(c, "", 13.5f, 0xDDFFFFFF, false);
            title.setMaxLines(6);
            title.setMinLines(2);
            title.setGravity(Gravity.CENTER_HORIZONTAL);
            title.setPadding(Ui.dp(c, 4), Ui.dp(c, 10), Ui.dp(c, 4), Ui.dp(c, 6));
            addView(title, new LayoutParams(-1, -2));
            sub = Ui.text(c, "", 12.5f, Ui.TEXT_DIM, false);
            sub.setSingleLine(true);
            sub.setEllipsize(TextUtils.TruncateAt.END);
            sub.setVisibility(GONE);
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
            setMeasuredDimension(size, Math.round(size * 0.9f));
        }

        @Override
        protected void onDraw(Canvas c) {
            if (game == null) return;
            int w = getWidth(), h = getHeight();
            RectF box;
            float rad;
            if (game.cover != null) {
                // box art at its own shape, standing on the bottom of the area
                Bitmap b = game.cover;
                float k = Math.min((float) w / b.getWidth(), (float) h / b.getHeight());
                float bw = b.getWidth() * k, bh = b.getHeight() * k;
                box = new RectF((w - bw) / 2, h - bh, (w + bw) / 2, h);
                rad = Ui.dp(getContext(), 7);
            } else {
                box = new RectF(0, 0, w, h);
                rad = r;
            }
            paint.setShader(null);
            paint.setColor(0x66000000);
            c.drawRoundRect(new RectF(box.left + 2, box.top + 6, box.right - 2, box.bottom + 6), rad, rad, paint);
            clip.reset();
            clip.addRoundRect(box, rad, rad, Path.Direction.CW);
            c.save();
            c.clipPath(clip);
            if (game.cover != null) {
                paint.setFilterBitmap(true);
                c.drawBitmap(game.cover, null, box, paint);
            } else {
                int t = game.tint;
                paint.setShader(new LinearGradient(0, 0, w, h, mix(t, 0xFF0A0E16, 0.45f), mix(t, 0xFF0A0E16, 0.8f), Shader.TileMode.CLAMP));
                c.drawRect(box, paint);
                paint.setShader(null);
                int s = (int) (Math.min(w, h) * 0.6f) / 32 * 32;
                if (s < 32) s = (int) (Math.min(w, h) * 0.6f);
                int x = (w - s) / 2, y = (h - s) / 2;
                c.drawBitmap(game.icon, new Rect(0, 0, 32, 32), new Rect(x, y, x + s, y + s), pix);
            }
            c.restore();
            float bh = Ui.dp(getContext(), 22), bm = Ui.dp(getContext(), 6);
            paint.setTextSize(Ui.dp(getContext(), 11));
            paint.setFakeBoldText(true);
            paint.setTextAlign(Paint.Align.CENTER);
            if (game.favorite) {
                RectF sr = new RectF(box.left + bm, box.top + bm, box.left + bm + bh, box.top + bm + bh);
                paint.setColor(0xD90A0E16);
                c.drawRoundRect(sr, bh / 2, bh / 2, paint);
                paint.setColor(0xFFFACC15);
                paint.setTextSize(Ui.dp(getContext(), 13));
                c.drawText("★", sr.centerX(), sr.centerY() - (paint.descent() + paint.ascent()) / 2, paint);
                paint.setTextSize(Ui.dp(getContext(), 11));
            }
            if (game.enhanced) {
                float bw = Ui.dp(getContext(), 42);
                RectF hr = new RectF(box.left + bm, box.bottom - bm - bh, box.left + bm + bw, box.bottom - bm);
                paint.setColor(0xD90A0E16);
                c.drawRoundRect(hr, bh / 2, bh / 2, paint);
                paint.setColor(0xFFF2C14E);
                c.drawText("✦ HD", hr.centerX(), hr.centerY() - (paint.descent() + paint.ascent()) / 2, paint);
            }
            if (game.resume) {
                float pw = Ui.dp(getContext(), 62);
                RectF pr = new RectF(box.right - bm - pw, box.top + bm, box.right - bm, box.top + bm + bh);
                paint.setColor(0xE60A0E16);
                c.drawRoundRect(pr, bh / 2, bh / 2, paint);
                paint.setColor(Ui.ACCENT);
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
