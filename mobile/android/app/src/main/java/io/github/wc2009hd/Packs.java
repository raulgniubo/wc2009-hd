package io.github.wc2009hd;

/** Game enhancement packs: extra improvements made for one game, on by themselves when it runs. */
final class Packs {
    static final class Option { final String key, title, detail; Option(String k, String t, String d) { key = k; title = t; detail = d; } }
    static final class Pack {
        final String code, title, shortTitle, about, needs, master;
        final String needsDetail = "The enhancements use an HD pack: a folder named WC2009-HD with the card art, the HD pictures and the redesign (about 1 GB). It isn't part of the game file, so it has to sit right next to it.";
        final String[] features;
        final Option[] options;
        Pack(String code, String title, String shortTitle, String about, String[] features, String needs, String master, Option... options) {
            this.code = code; this.title = title; this.shortTitle = shortTitle; this.about = about; this.features = features;
            this.needs = needs; this.master = master; this.options = options;
        }
    }

    static final Pack[] ALL = {
        new Pack("CY8", "Yu-Gi-Oh! 5D's World Championship 2009", "Yu-Gi-Oh! 5D's WC2009",
                "An HD makeover for the card game, made just for it.",
                new String[] {
                    "HD art for every card, in duels and in the deck editor",
                    "High-resolution characters, backgrounds, menus and 3D world",
                    "Sharper game text, with the same look",
                    "Optional redesign: new life point panel, duel buttons and icons, card text in a clean font",
                },
                "WC2009-HD",
                "hdMode"),
    };

    static Pack forCode(String code) {
        if (code == null) return null;
        for (Pack p : ALL) if (code.startsWith(p.code)) return p;
        return null;
    }

    private Packs() {}
}
