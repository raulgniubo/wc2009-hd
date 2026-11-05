#include "WC09Overlay.h"

#include <QDir>
#include <QFileInfo>
#include <cmath>
#include <QDateTime>

#include "NDS.h"
#include "NDSCart.h"

using namespace melonDS;

void WC09Overlay::update(NDS* nds, const QString& romPath)
{
    bool want = false;
    if (nds)
    {
        auto* cart = nds->NDSCartSlot.GetCart();
        if (cart)
        {
            const char* gc = cart->GetHeader().GameCode;
            want = gc[0] == 'C' && gc[1] == 'Y' && gc[2] == '8' && gc[3] == 'E';
        }
    }
    if (want != active)
    {
        active = want;
        WC09::SetActive(want);
    }
    if (romPath != lastRom)
    {
        lastRom = romPath;
        cache.clear(); missing.clear(); order.clear(); gen++;
        QFileInfo fi(romPath);
        QDir dir = fi.dir();
        folder = dir.exists("HDCards") ? dir.filePath("HDCards") : QString();
    }
}

const QImage* WC09Overlay::image(const WC09::CardQuad& q)
{
    if (folder.isEmpty()) return nullptr;
    auto it = cache.find(q.Card);
    if (it != cache.end()) return &it.value();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto miss = missing.find(q.Card);
    if (miss != missing.end() && now - miss.value() < 3000) return nullptr;   // lets you drop images in while playing

    QStringList names;
    if (q.Passcode) names << QString::number(q.Passcode) << QString("%1").arg(q.Passcode, 8, 10, QChar('0'));
    names << QString("card_%1").arg(q.Card);

    QImage img;
    for (const QString& n : names)
    {
        for (const char* ext : {"png", "jpg", "jpeg", "webp"})
        {
            QString p = QDir(folder).filePath(n + "." + ext);
            if (QFileInfo::exists(p) && img.load(p)) break;
        }
        if (!img.isNull()) break;
    }
    if (img.isNull()) { missing.insert(q.Card, now); return nullptr; }
    missing.remove(q.Card);

    // keep memory sane: cap at 1024px tall, premultiplied RGBA for both painters
    if (img.height() > 1024) img = img.scaledToHeight(1024, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);

    if (order.size() >= kMaxCached) { cache.remove(order.front()); order.pop_front(); }
    order.push_back(q.Card);
    gen++;
    return &cache.insert(q.Card, img).value();
}

std::array<std::array<float, 3>, 4> WC09Overlay::projectiveUV(const WC09::CardQuad& q)
{
    // q-weights from the diagonal intersection (standard quad projective mapping)
    std::array<std::array<float, 3>, 4> out;
    float x0 = q.X[0], y0 = q.Y[0], x1 = q.X[1], y1 = q.Y[1];
    float x2 = q.X[2], y2 = q.Y[2], x3 = q.X[3], y3 = q.Y[3];
    // diagonals 0-2 and 1-3
    float ax = x2 - x0, ay = y2 - y0, bx = x3 - x1, by = y3 - y1;
    float den = ax * by - ay * bx;
    float d[4] = {1, 1, 1, 1};
    if (std::fabs(den) > 1e-6f)
    {
        float s = ((x1 - x0) * by - (y1 - y0) * bx) / den;   // along 0->2
        float t = ((x1 - x0) * ay - (y1 - y0) * ax) / den;   // along 1->3
        if (s > 0 && s < 1 && t > 0 && t < 1)
        {
            float l02 = std::hypot(ax, ay), l13 = std::hypot(bx, by);
            d[0] = s * l02; d[2] = (1 - s) * l02;
            d[1] = t * l13; d[3] = (1 - t) * l13;
        }
    }
    for (int i = 0; i < 4; i++)
    {
        float qq = (d[(i + 2) % 4] > 0) ? (d[i] + d[(i + 2) % 4]) / d[(i + 2) % 4] : 1.f;
        out[i] = {q.U[i] * qq, q.V[i] * qq, qq};
    }
    return out;
}
