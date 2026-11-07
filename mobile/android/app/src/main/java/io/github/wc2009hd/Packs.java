package io.github.wc2009hd;

/** Game enhancement packs: extra improvements made for one game, on by themselves when it runs. */
final class Packs {
    static final class Option { final String key, title, detail; Option(String k, String t, String d) { key = k; title = t; detail = d; } }
    static final class Pack {
        final String code, title, shortTitle, about, needs, master;
        final String[] features;
        final Option[] options;
        Pack(String code, String title, String shortTitle, String about, String[] features, String needs, String master, Option... options) {
            this.code = code; this.title = title; this.shortTitle = shortTitle; this.about = about; this.features = features;
            this.needs = needs; this.master = master; this.options = options;
        }
    }

    static final Pack[] ALL = {
        new Pack("CY8", "Yu-Gi-Oh! 5D's World Championship 2009", "Yu-Gi-Oh! 5D's WC2009",
                "A full HD makeover for the card game, made just for it.",
                new String[] {
                    "HD art for every card, in duels and in the deck editor",
                    "Card names and descriptions in a clean, sharp font",
                    "Redesigned life point panel, phase buttons and duel screen icons",
                    "High-resolution menus, backgrounds and dialogs",
                },
                "For the card art, keep the HDCards folder next to the game.",
                "hdAll",
                new Option("hdCards", "HD card art", "Real card images, from the HDCards folder next to the game."),
                new Option("hdText", "HD text and menus", "Card names, descriptions and panels in a clean font.")),
    };

    static Pack forCode(String code) {
        if (code == null) return null;
        for (Pack p : ALL) if (code.startsWith(p.code)) return p;
        return null;
    }

    private Packs() {}
}
