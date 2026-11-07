#include "WC09Overlay.h"

#include <QDir>
#include <QFileInfo>
#include <cmath>
#include <QDateTime>
#include <QTransform>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include "NDS.h"
#include "NDSCart.h"
#include "WC09Battle.h"
#include <cstring>

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

        // HD images for things the core swaps inside the renderer (battle monsters):
        // HDCards/<name>.png, e.g. HDCards/battle/7736_r.png
        QString f = folder;
        WC09::SetHDImageProvider([f](const std::string& name, std::vector<u8>& rgba, int& w, int& h) {
            auto load = [&](const QString& n) {
                QImage im;
                for (const char* ext : {".png", ".webp", ".jpg"})
                    if (im.load(QDir(f).filePath(n + ext))) break;
                return im;
            };
            QString n = QString::fromStdString(name);
            QImage img;
            if (n.startsWith("lppanel/"))
            {
                int lp0, lp1, turn, phase; bool right;
                if (WC09::GetLpPanel(n.mid(8).toInt(), lp0, lp1, turn, phase, right))
                    img = WC09Overlay::renderLpPanel(lp0, lp1, turn, phase, right, 8.f);
            }
            else if (n.startsWith("statpanel/"))
            {
                WC09::StatPanelInfo info;
                if (WC09::GetStatPanel(n.mid(10).toInt(), info))
                    img = WC09Overlay::renderStatPanel(info, 8.f);
            }
            else if (n.startsWith("decktext/") || n.startsWith("deckname/"))
                img = WC09Overlay::renderDeckText(n.mid(9).toInt(), 8.f, n.startsWith("deckname/"));
            else if (n.startsWith("textpanel/"))
            {
                QString id = n.mid(10);
                bool shortVer = id.endsWith('s');
                if (shortVer) id.chop(1);
                img = WC09Overlay::renderTextPanel(id.toInt(), 8.f, shortVer ? WC09::kPanelShortH : WC09::kPanelH);
            }
            else
                img = load(n);
            // left-facing battle images are mostly mirrors of the right-facing ones
            if (img.isNull() && n.startsWith("BattleMonsterL") && n.endsWith("_l"))
            {
                QString r = n; r.replace("BattleMonsterL", "BattleMonsterR"); r.chop(2); r += "_r";
                img = load(r);
                if (!img.isNull()) img = img.transformed(QTransform().scale(-1, 1));
            }
            if (img.isNull()) return false;
            img = img.convertToFormat(QImage::Format_RGBA8888);   // straight alpha
            w = img.width(); h = img.height();
            rgba.resize((size_t)w * h * 4);
            for (int y = 0; y < h; y++) memcpy(&rgba[(size_t)y * w * 4], img.constScanLine(y), (size_t)w * 4);
            return true;
        });
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

int WC09Overlay::detailCard(const WC09::FrameCards& fc)
{
    for (const auto& q : fc.Quads)
        if (q.TexSize == 1 && q.Screen == 0 && q.X[0] >= 4 && q.X[0] <= 12 && q.Y[0] >= 16 && q.Y[0] <= 28)
            return q.Card;
    return -1;
}

const QImage* WC09Overlay::textPanel(int card, float scale)
{
    int sc = std::max(1, (int)std::lround(scale * 4.f));       // quantize to 1/4 px
    if (card == panelCard && sc == panelScale && !panelImg.isNull()) return &panelImg;
    QImage img = renderTextPanel(card, sc / 4.f, kPanelH);
    if (img.isNull()) return nullptr;
    panelImg = img; panelCard = card; panelScale = sc;
    return &panelImg;
}

QImage WC09Overlay::renderTextPanel(int card, float k, int heightDS)
{
    QString name = QString::fromStdString(WC09::CardNameEn(card));
    QString desc = QString::fromStdString(WC09::CardDescEn(card));
    if (name.isEmpty()) return QImage();
    // the game marks line breaks with '\n' already; collapse doubled spaces
    desc.replace("  ", " ");

    QImage img((int)std::ceil(kPanelW * k), (int)std::ceil(heightDS * k), QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.scale(k, k);

    const QColor fill(14, 26, 56, 255), edge(140, 190, 255, 220);
    auto panel = [&](QRectF r) {
        QPainterPath path; path.addRoundedRect(r, 3.5, 3.5);
        p.fillPath(path, fill);
        p.setPen(QPen(edge, 0.8));
        p.drawPath(path);
    };

    QFont base = QFontDatabase::systemFont(QFontDatabase::GeneralFont);

    // name bar
    QRectF nameR(4, 0.5, 168, 17.5);
    panel(nameR);
    QFont nf = base; nf.setBold(true);
    float ns = 11.f;
    for (; ns > 6.f; ns -= 0.25f)
    {
        nf.setPixelSize(std::max(1, (int)std::lround(ns * k)));
        QFontMetricsF fm(nf);
        if (fm.horizontalAdvance(name) / k <= nameR.width() - 8) break;
    }
    p.save(); p.resetTransform();
    p.setFont(nf);
    QRectF nameDev(nameR.x() * k, nameR.y() * k, nameR.width() * k, nameR.height() * k);
    p.setPen(QColor(0, 0, 0, 160)); p.drawText(nameDev.translated(0.6 * k, 0.6 * k), Qt::AlignCenter, name);
    p.setPen(QColor(255, 255, 255)); p.drawText(nameDev, Qt::AlignCenter, name);
    p.restore();

    // description box: fit the whole text, no scrolling
    QRectF boxR(60, 21, kPanelW - 60.5, heightDS - 21.5);
    panel(boxR);
    QRectF textR = boxR.adjusted(4, 3, -4, -3);
    QRectF textDev(textR.x() * k, textR.y() * k, textR.width() * k, textR.height() * k);
    QFont tf = base;
    const int flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
    for (float ts = 10.f; ts >= 4.f; ts -= 0.25f)
    {
        tf.setPixelSize(std::max(1, (int)std::lround(ts * k)));
        QFontMetricsF fm(tf);
        if (fm.boundingRect(textDev, flags, desc).height() <= textDev.height()) break;
    }
    p.resetTransform();
    p.setFont(tf);
    p.setPen(QColor(0, 0, 0, 150)); p.drawText(textDev.translated(0.5 * k, 0.5 * k), flags, desc);
    p.setPen(QColor(240, 246, 255)); p.drawText(textDev, flags, desc);
    p.end();
    return img;
}

QImage WC09Overlay::renderDeckText(int card, float k, bool nameOnly)
{
    QString name = QString::fromStdString(WC09::CardNameEn(card));
    QString desc = QString::fromStdString(WC09::CardDescEn(card));
    if (name.isEmpty()) return QImage();
    desc.replace("  ", " ");
    const float W = nameOnly ? WC09::kDeckNameW : WC09::kDeckDescW;
    const float H = nameOnly ? WC09::kDeckNameH : WC09::kDeckDescH;
    QImage img((int)std::ceil(W * k), (int)std::ceil(H * k), QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // white text with a solid dark outline: the compositor keeps only opaque pixels, and the
    // game's own panel shows through everywhere else
    auto drawOutlined = [&](const QRectF& r, int flags, const QString& text, const QFont& f) {
        QPainterPath path;
        QFontMetricsF fm(f);
        // lay out line by line with word wrap (QPainterPath::addText has no wrapping)
        QStringList paragraphs = text.split('\n');
        float y = r.y() + fm.ascent();
        for (const QString& para : paragraphs)
        {
            QStringList words = para.split(' ', Qt::SkipEmptyParts);
            QString line;
            auto flush = [&]() {
                float x = r.x();
                if (flags & Qt::AlignHCenter) x = r.x() + (r.width() - fm.horizontalAdvance(line)) / 2;
                path.addText(QPointF(x, y), f, line);
                y += fm.lineSpacing(); line.clear();
            };
            if (words.isEmpty()) { y += fm.lineSpacing(); continue; }
            for (const QString& w : words)
            {
                QString t = line.isEmpty() ? w : line + ' ' + w;
                if (!line.isEmpty() && fm.horizontalAdvance(t) > r.width()) { flush(); line = w; }
                else line = t;
            }
            if (!line.isEmpty()) flush();
        }
        if (flags & Qt::AlignVCenter)
        {
            float used = y - fm.lineSpacing() + fm.descent() - r.y();
            path.translate(0, (r.height() - used) / 2);
        }
        p.setPen(QPen(QColor(16, 20, 40), 0.22f * f.pixelSize(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255));
        p.drawPath(path);
    };
    auto fits = [&](const QRectF& r, const QString& text, const QFont& f) {
        QFontMetricsF fm(f);
        return fm.boundingRect(r, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text).height() <= r.height();
    };

    QFont base = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (nameOnly)
    {
        QFont f = base; f.setBold(true);
        QRectF r(4 * k, 0, (W - 8) * k, H * k);
        for (float ts = 12.f; ts > 6.f; ts -= 0.25f)
        {
            f.setPixelSize(std::max(1, (int)std::lround(ts * k)));
            if (QFontMetricsF(f).horizontalAdvance(name) <= r.width()) break;
        }
        drawOutlined(r, Qt::AlignHCenter | Qt::AlignVCenter, name, f);
    }
    else
    {
        QFont f = base;
        QRectF r(2 * k, 2 * k, (W - 5) * k, (H - 4) * k);
        for (float ts = 11.f; ts >= 4.f; ts -= 0.25f)
        {
            f.setPixelSize(std::max(1, (int)std::lround(ts * k)));
            if (fits(r, desc, f)) break;
        }
        drawOutlined(r, Qt::AlignLeft, desc, f);
    }
    p.end();
    return img;
}

QImage WC09Overlay::renderStatPanel(const WC09::StatPanelInfo& info, float k)
{
    // the usual text panel at x = kPanelX, plus ATK/DEF on a small plate under the card and the
    // level badge on the card's corner
    QImage panel = renderTextPanel(info.Card, k, info.Bubble ? WC09::kPanelShortH : WC09::kPanelH);
    if (panel.isNull()) return panel;
    const int W = WC09::kPanelX + WC09::kPanelW, H = WC09::kStatsY + WC09::kStatsH - WC09::kPanelY;
    QImage img((int)std::ceil(W * k), (int)std::ceil(H * k), QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    // alpha 0 = show the game's layer (left of the panel, the ATK/DEF row when unknown), faint
    // alpha = hide it (the panel area, as before: the game's own text and icons stay hidden)
    p.fillRect(QRectF(WC09::kPanelX * k, 0, WC09::kPanelW * k, panel.height()), QColor(0, 0, 0, 77));
    p.drawImage(QPointF(WC09::kPanelX * k, 0), panel);
    p.scale(k, k);
    const QColor fill(14, 26, 56), edge(140, 190, 255, 230);
    QFont f = QFontDatabase::systemFont(QFontDatabase::GeneralFont); f.setBold(true);

    if (info.Atk >= 0)
    {
        const float y0 = WC09::kStatsY - WC09::kPanelY;
        QPainterPath plate; plate.addRoundedRect(QRectF(13, y0 - 0.5, 84, WC09::kStatsH + 1), 3, 3);
        p.fillPath(plate, fill);
        p.setPen(QPen(edge, 0.7)); p.drawPath(plate);
        p.save(); p.resetTransform();
        f.setPixelSize(std::max(1, (int)std::lround(10.5f * k)));
        p.setFont(f);
        QRectF ra(14 * k, (y0 - 0.5f) * k, 31 * k, (WC09::kStatsH + 1) * k);
        QRectF rd(64 * k, (y0 - 0.5f) * k, 31 * k, (WC09::kStatsH + 1) * k);
        p.setPen(QColor(255, 255, 255));
        p.drawText(ra, Qt::AlignRight | Qt::AlignVCenter, QString::number(info.Atk));
        p.drawText(rd, Qt::AlignLeft | Qt::AlignVCenter, QString::number(info.Def));
        p.restore();
    }

    if (info.Level > 0)
    {
        // "★ 4" on a rounded plate, right edge where the game's badge ended
        f.setPixelSize(std::max(1, (int)std::lround(8.5f * k)));
        QString num = QString::number(info.Level);
        float tw = QFontMetricsF(f).horizontalAdvance(num) / k;
        const float h = 10.f, star = 7.5f, pad = 2.f;
        // wide enough to cover the card's printed stars (about 6.6 px each at this size)
        float w = std::max(pad + star + 1.5f + tw + pad + 0.5f, info.Level * 6.4f + 2.f);
        float x1 = info.BadgeRight + 1.f, x0 = x1 - w;
        float yc = info.BadgeMidY + 0.5f - WC09::kPanelY, y0 = yc - h / 2;
        QPainterPath plate; plate.addRoundedRect(QRectF(x0, y0, w, h), 3.f, 3.f);
        p.fillPath(plate, fill);
        p.setPen(QPen(edge, 0.7)); p.drawPath(plate);
        // five-pointed star
        QPainterPath sp;
        float gx = x0 + (w - (star + 2.f + tw)) / 2;   // star and number centred together
        float cx = gx + star / 2, cy = yc, ro = star / 2, ri = ro * 0.45f;
        for (int i = 0; i < 10; i++)
        {
            float a = -M_PI / 2 + i * M_PI / 5, r = (i & 1) ? ri : ro;
            QPointF pt(cx + r * std::cos(a), cy + r * std::sin(a));
            if (i == 0) sp.moveTo(pt); else sp.lineTo(pt);
        }
        sp.closeSubpath();
        QLinearGradient g(cx, cy - ro, cx, cy + ro);
        g.setColorAt(0, QColor(255, 236, 140)); g.setColorAt(1, QColor(240, 160, 30));
        p.setPen(QPen(QColor(120, 60, 0), 0.5)); p.setBrush(g); p.drawPath(sp);
        p.save(); p.resetTransform();
        p.setFont(f); p.setPen(QColor(255, 255, 255));
        p.drawText(QRectF((gx + star + 2.f) * k, y0 * k, (tw + 2) * k, h * k), Qt::AlignLeft | Qt::AlignVCenter, num);
        p.restore();
    }
    p.end();
    return img;
}

QImage WC09Overlay::renderLpPanel(int lp0, int lp1, int turn, int phase, bool turnRight, float k)
{
    const float W = WC09::kLpW, H = WC09::kLpH;
    QImage img((int)std::ceil(W * k), (int)std::ceil(H * k), QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.scale(k, k);
    // panel
    QLinearGradient bg(0, 0, 0, H);
    bg.setColorAt(0, QColor(30, 50, 100)); bg.setColorAt(1, QColor(14, 24, 54));
    QPainterPath panel; panel.addRoundedRect(QRectF(0.5, 0.5, W - 1, H - 1), 6, 6);
    p.fillPath(panel, bg);
    p.setPen(QPen(QColor(120, 175, 255, 230), 0.8)); p.drawPath(panel);

    auto bar = [&](QRectF track, float ratio, bool fromLeft) {
        QPainterPath tp; tp.addRoundedRect(track, track.height() / 2, track.height() / 2);
        p.fillPath(tp, QColor(8, 14, 34));
        ratio = std::clamp(ratio, 0.f, 1.f);
        if (ratio > 0)
        {
            float fw = std::max((float)track.height(), (float)track.width() * ratio);
            QRectF f = fromLeft ? QRectF(track.x(), track.y(), fw, track.height())
                                : QRectF(track.right() - fw, track.y(), fw, track.height());
            QLinearGradient g(fromLeft ? track.left() : track.right(), 0, fromLeft ? track.right() : track.left(), 0);
            g.setColorAt(0, QColor(255, 70, 40)); g.setColorAt(0.45, QColor(255, 170, 40)); g.setColorAt(1, QColor(255, 236, 120));
            QPainterPath fp; fp.addRoundedRect(f, track.height() / 2, track.height() / 2);
            p.fillPath(fp, g);
            QLinearGradient gl(0, f.top(), 0, f.bottom());
            gl.setColorAt(0, QColor(255, 255, 255, 110)); gl.setColorAt(0.5, QColor(255, 255, 255, 0));
            p.fillPath(fp, gl);
        }
        p.setPen(QPen(QColor(120, 175, 255, 160), 0.5)); p.setBrush(Qt::NoBrush); p.drawPath(tp);
    };
    auto text = [&](QRectF r, const QString& t, float px, int flags, QColor col = QColor(255, 255, 255)) {
        QFont f = QFontDatabase::systemFont(QFontDatabase::GeneralFont); f.setBold(true);
        f.setPixelSize(std::max(1, (int)std::lround(px * k)));
        QPainterPath path;
        QFontMetricsF fm(f);
        QRectF dr(r.x() * k, r.y() * k, r.width() * k, r.height() * k);
        float tw = fm.horizontalAdvance(t);
        float x = (flags & Qt::AlignRight) ? dr.right() - tw : (flags & Qt::AlignHCenter) ? dr.center().x() - tw / 2 : dr.left();
        float y = dr.center().y() + (fm.ascent() - fm.descent()) / 2;
        path.addText(QPointF(x, y), f, t);
        p.save(); p.resetTransform();
        p.setPen(QPen(QColor(6, 10, 28, 230), 0.22f * f.pixelSize(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush); p.drawPath(path);
        p.setPen(Qt::NoPen); p.setBrush(col); p.drawPath(path);
        p.restore();
    };
    // player 1: top row, bar from the left; player 2: bottom row, bar from the right
    bar(QRectF(21, 3, W - 24, 10), lp0 / 8000.f, true);
    bar(QRectF(3, H - 13, W - 24, 10), lp1 / 8000.f, false);
    text(QRectF(23, 2, 60, 12), QString::number(lp0), 10.f, Qt::AlignLeft);
    text(QRectF(W - 84, H - 14, 60, 12), QString::number(lp1), 10.f, Qt::AlignRight);
    // turn chip
    if (turn >= 0)
    {
        // on the side of the player whose turn it is (top-left: player 1, bottom-right: player 2)
        QRectF cr = turnRight ? QRectF(W - 17, H - 15.5, 14, 14) : QRectF(3, 1.5, 14, 14);
        QPainterPath cp; cp.addEllipse(cr);
        p.fillPath(cp, turnRight ? QColor(170, 40, 70) : QColor(44, 74, 150)); p.setPen(QPen(QColor(200, 225, 255), 0.7)); p.drawPath(cp);
        text(cr, QString::number(turn), turn >= 10 ? 7.f : 8.5f, Qt::AlignHCenter);
    }
    // phase pill
    static const char* const kNames[6] = {"Draw Phase", "Standby Phase", "Main Phase 1", "Battle Phase", "Main Phase 2", "End Phase"};
    if (phase >= 0 && phase < 6)
    {
        QRectF pr(W / 2 - 34, 15, 68, 11);
        QPainterPath pp; pp.addRoundedRect(pr, 5.5, 5.5);
        QLinearGradient pg(0, pr.top(), 0, pr.bottom());
        QColor c0 = phase == 3 ? QColor(245, 95, 110) : QColor(80, 160, 255), c1 = phase == 3 ? QColor(175, 35, 60) : QColor(34, 90, 210);
        pg.setColorAt(0, c0); pg.setColorAt(1, c1);
        p.fillPath(pp, pg); p.setPen(QPen(QColor(220, 240, 255), 0.6)); p.drawPath(pp);
        text(pr, kNames[phase], 7.4f, Qt::AlignHCenter);
    }
    p.end();
    return img;
}
