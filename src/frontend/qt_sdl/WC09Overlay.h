/*
    WC09 HD cards, frontend side: loads user-supplied HD card images and
    computes how to draw them over the cards the game is showing.

    HD images live in a folder named "HDCards" next to the ROM:
        HDCards/<passcode>.png|jpg|jpeg|webp   (e.g. 70902743.jpg)
        HDCards/card_<index>.png               (game-only cards without a passcode)
    An image is the full card face (portrait, any resolution).
*/
#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QList>
#include <array>

#include "WC09Cards.h"
#include "WC09Battle.h"

namespace melonDS { class NDS; }

class WC09Overlay
{
public:
    // Enables the core hook if the loaded ROM is WC2009 US. Call once per frame; cheap.
    void update(melonDS::NDS* nds, const QString& romPath);

    bool enabled() const { return active && !folder.isEmpty() && melonDS::WC09::GetHDCardsEnabled(); }

    // HD image for a card, or nullptr if none was supplied. Cached (LRU-ish, bounded).
    const QImage* image(const melonDS::WC09::CardQuad& q);

    // Changes every time a new image gets loaded (lets GL code know to upload).
    quint64 generation() const { return gen; }

    // Projective texture coords (u*q, v*q, q) per vertex so a flat card stays
    // undistorted on a tilted quad.
    static std::array<std::array<float, 3>, 4> projectiveUV(const melonDS::WC09::CardQuad& q);

    // Duel card-detail text panel (top screen): name + full description in a real font.
    // Region in DS pixels of the top screen; the image covers it (transparent outside panels).
    // Height 124 keeps clear of the speech bubbles when we draw on top (software 2D);
    // the OpenGL renderer swaps the text layer itself and uses the full height.
    static constexpr int kPanelX = 40, kPanelY = 1, kPanelW = 213, kPanelH = 124;
    // Card shown in the duel's card-detail view, or -1.
    static int detailCard(const melonDS::WC09::FrameCards& fc);
    // Panel image rendered at `scale` device pixels per DS pixel (cached).
    const QImage* textPanel(int card, float scale);
    static QImage renderTextPanel(int card, float scale, int heightDS);
    // deck editor detail view: description (or the name) as outlined text on transparent
    static QImage renderDeckText(int card, float scale, bool nameOnly);
    // duel card view panel plus the ATK/DEF row under the card
    // duel life point panel (top screen, under the card view)
    static QImage renderLpPanel(int lp0, int lp1, int turn, int phase, bool turnRight, float scale);
    static QImage renderStatPanel(const melonDS::WC09::StatPanelInfo& info, float scale);

private:
    int panelCard = -1; int panelScale = 0; QImage panelImg;
    bool active = false;
    QString folder;
    QString lastRom;
    QHash<int, QImage> cache;       // key = card index
    QHash<int, qint64> missing;     // card -> time of last failed lookup (ms); retried after 3 s
    QList<int> order;
    quint64 gen = 0;
    static constexpr int kMaxCached = 48;
};
