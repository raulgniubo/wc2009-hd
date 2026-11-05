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

namespace melonDS { class NDS; }

class WC09Overlay
{
public:
    // Enables the core hook if the loaded ROM is WC2009 US. Call once per frame; cheap.
    void update(melonDS::NDS* nds, const QString& romPath);

    bool enabled() const { return active && !folder.isEmpty(); }

    // HD image for a card, or nullptr if none was supplied. Cached (LRU-ish, bounded).
    const QImage* image(const melonDS::WC09::CardQuad& q);

    // Changes every time a new image gets loaded (lets GL code know to upload).
    quint64 generation() const { return gen; }

    // Projective texture coords (u*q, v*q, q) per vertex so a flat card stays
    // undistorted on a tilted quad.
    static std::array<std::array<float, 3>, 4> projectiveUV(const melonDS::WC09::CardQuad& q);

private:
    bool active = false;
    QString folder;
    QString lastRom;
    QHash<int, QImage> cache;       // key = card index
    QHash<int, bool> missing;       // remembered misses
    QList<int> order;
    quint64 gen = 0;
    static constexpr int kMaxCached = 48;
};
