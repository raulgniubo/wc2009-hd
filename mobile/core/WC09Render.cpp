// Mobile versions of the desktop WC09Overlay renderers (frontend/qt_sdl/WC09Overlay.cpp): same
// layout and colours, drawn with canvas_ity, images decoded with stb_image.
#include "WC09Render.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <memory>
#include <sys/stat.h>
#include <dirent.h>
#include <thread>
#include <unordered_set>
#include <atomic>

#define CANVAS_ITY_IMPLEMENTATION
#include "third_party/canvas_ity.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "third_party/stb_image.h"

#include "WC09Battle.h"

using namespace melonDS;

namespace wc09m
{
namespace
{
std::vector<unsigned char> FontMedium, FontBold;

std::vector<unsigned char> ReadAll(const std::string& path)
{
    std::vector<unsigned char> d;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return d;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0) { d.resize(n); if (fread(d.data(), 1, n, f) != (size_t)n) d.clear(); }
    fclose(f);
    return d;
}

struct RGBA { float r, g, b, a; };
RGBA C(int r, int g, int b, int a = 255) { return {r / 255.f, g / 255.f, b / 255.f, a / 255.f}; }

// a canvas in DS pixels scaled by k, with the helpers the panels need
struct Painter
{
    int W, H;
    float K;
    canvas_ity::canvas cv;
    Painter(float wDS, float hDS, float k) : W((int)std::ceil(wDS * k)), H((int)std::ceil(hDS * k)), K(k), cv(W, H) {}

    void roundRect(float x, float y, float w, float h, float r)
    {
        r = std::min(r, std::min(w, h) / 2);
        cv.begin_path();
        cv.move_to((x + r) * K, y * K);
        cv.arc_to((x + w) * K, y * K, (x + w) * K, (y + h) * K, r * K);
        cv.arc_to((x + w) * K, (y + h) * K, x * K, (y + h) * K, r * K);
        cv.arc_to(x * K, (y + h) * K, x * K, y * K, r * K);
        cv.arc_to(x * K, y * K, (x + w) * K, y * K, r * K);
        cv.close_path();
    }
    void ellipse(float x, float y, float w, float h)
    {
        cv.begin_path();
        cv.arc((x + w / 2) * K, (y + h / 2) * K, std::min(w, h) / 2 * K, 0, 6.2831853f);
        cv.close_path();
    }
    void solid(canvas_ity::brush_type t, RGBA c) { cv.set_color(t, c.r, c.g, c.b, c.a); }
    void vgrad(canvas_ity::brush_type t, float y0, float y1, std::initializer_list<std::pair<float, RGBA>> stops)
    {
        cv.set_linear_gradient(t, 0, y0 * K, 0, y1 * K);
        for (auto& s : stops) cv.add_color_stop(t, s.first, s.second.r, s.second.g, s.second.b, s.second.a);
    }
    void hgrad(canvas_ity::brush_type t, float x0, float x1, std::initializer_list<std::pair<float, RGBA>> stops)
    {
        cv.set_linear_gradient(t, x0 * K, 0, x1 * K, 0);
        for (auto& s : stops) cv.add_color_stop(t, s.first, s.second.r, s.second.g, s.second.b, s.second.a);
    }
    void strokeWidth(float w) { cv.set_line_width(std::max(0.01f, w * K)); }

    // fonts: px in device pixels
    bool font(bool bold, float px)
    {
        std::vector<unsigned char>& f = bold ? FontBold : FontMedium;
        if (f.empty()) return false;
        return cv.set_font(f.data(), (int)f.size(), px);
    }
    float measure(const std::string& t) { return cv.measure_text(t.c_str()); }

    std::vector<u8> pixels()
    {
        std::vector<u8> out((size_t)W * H * 4);
        cv.get_image_data(out.data(), W, H, W * 4, 0, 0);
        return out;
    }
};

constexpr float kAscent = 0.969f, kDescent = 0.241f, kLine = 1.21f;

// word wrap for one paragraph-aware text at the current font
std::vector<std::string> Wrap(Painter& p, const std::string& text, float width)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t i = 0;
        bool any = false;
        while (i < para.size())
        {
            while (i < para.size() && para[i] == ' ') i++;
            size_t j = para.find(' ', i);
            std::string w = para.substr(i, j == std::string::npos ? std::string::npos : j - i);
            i = j == std::string::npos ? para.size() : j;
            if (w.empty()) continue;
            std::string t = line.empty() ? w : line + " " + w;
            if (!line.empty() && p.measure(t) > width) { lines.push_back(line); line = w; }
            else line = t;
            any = true;
        }
        if (!line.empty() || !any) lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string Collapse(std::string s)
{
    size_t p;
    while ((p = s.find("  ")) != std::string::npos) s.replace(p, 2, " ");
    return s;
}

// text drawn with a dark outline, like QPainterPath stroke + fill on the desktop
void OutlinedText(Painter& p, const std::string& t, float x, float y, float px, RGBA fill, RGBA outline, float outlineW)
{
    p.cv.line_join = canvas_ity::rounded;
    p.cv.line_cap = canvas_ity::circle;
    p.solid(canvas_ity::stroke_style, outline);
    p.cv.set_line_width(outlineW * px);
    p.cv.stroke_text(t.c_str(), x, y);
    p.solid(canvas_ity::fill_style, fill);
    p.cv.fill_text(t.c_str(), x, y);
}

std::vector<u8> TextPanel(int card, float k, int heightDS, int& w, int& h)
{
    std::string name = WC09::CardNameEn(card), desc = Collapse(WC09::CardDescEn(card));
    if (name.empty()) return {};
    Painter p(WC09::kPanelW, heightDS, k);
    const RGBA fill = C(14, 26, 56), edge = C(140, 190, 255, 220);
    auto panel = [&](float x, float y, float pw, float ph) {
        p.roundRect(x, y, pw, ph, 3.5f);
        p.solid(canvas_ity::fill_style, fill); p.cv.fill();
        p.solid(canvas_ity::stroke_style, edge); p.strokeWidth(0.8f); p.cv.stroke();
    };
    // name bar
    float nx = 4, ny = 0.5f, nw = 168, nh = 17.5f;
    panel(nx, ny, nw, nh);
    float ns = 11.f;
    for (; ns > 6.f; ns -= 0.25f) { p.font(true, std::round(ns * k)); if (p.measure(name) / k <= nw - 8) break; }
    p.cv.text_align = canvas_ity::center; p.cv.text_baseline = canvas_ity::alphabetic;
    float px = std::round(ns * k);
    float by = (ny + nh / 2) * k + (kAscent - kDescent) / 2 * px;
    p.solid(canvas_ity::fill_style, C(0, 0, 0, 160)); p.cv.fill_text(name.c_str(), (nx + nw / 2 + 0.6f) * k, by + 0.6f * k);
    p.solid(canvas_ity::fill_style, C(255, 255, 255)); p.cv.fill_text(name.c_str(), (nx + nw / 2) * k, by);

    // description box
    float bx = 60, byy = 21, bw = WC09::kPanelW - 60.5f, bh = heightDS - 21.5f;
    panel(bx, byy, bw, bh);
    float tx = (bx + 4) * k, ty = (byy + 3) * k, tw = (bw - 8) * k, th = (bh - 6) * k;
    std::vector<std::string> lines;
    float ts = 10.f;
    for (; ts >= 4.f; ts -= 0.25f)
    {
        p.font(false, std::round(ts * k));
        lines = Wrap(p, desc, tw);
        if (lines.size() * kLine * std::round(ts * k) <= th) break;
    }
    px = std::round(ts * k);
    p.cv.text_align = canvas_ity::leftward;
    for (size_t i = 0; i < lines.size(); i++)
    {
        float y = ty + kAscent * px + i * kLine * px;
        p.solid(canvas_ity::fill_style, C(0, 0, 0, 150)); p.cv.fill_text(lines[i].c_str(), tx + 0.5f * k, y + 0.5f * k);
        p.solid(canvas_ity::fill_style, C(240, 246, 255)); p.cv.fill_text(lines[i].c_str(), tx, y);
    }
    w = p.W; h = p.H;
    return p.pixels();
}

std::vector<u8> DeckText(int card, float k, bool nameOnly, int& w, int& h)
{
    std::string name = WC09::CardNameEn(card), desc = Collapse(WC09::CardDescEn(card));
    if (name.empty()) return {};
    const float W = nameOnly ? WC09::kDeckNameW : WC09::kDeckDescW;
    const float H = nameOnly ? WC09::kDeckNameH : WC09::kDeckDescH;
    Painter p(W, H, k);
    const RGBA white = C(255, 255, 255), dark = C(16, 20, 40);
    if (nameOnly)
    {
        float rw = (W - 8) * k;
        float ts = 12.f;
        for (; ts > 6.f; ts -= 0.25f) { p.font(true, std::round(ts * k)); if (p.measure(name) <= rw) break; }
        float px = std::round(ts * k);
        p.cv.text_align = canvas_ity::center;
        OutlinedText(p, name, W / 2 * k, H / 2 * k + (kAscent - kDescent) / 2 * px, px, white, dark, 0.22f);
    }
    else
    {
        float rx = 2 * k, ry = 2 * k, rw = (W - 5) * k, rh = (H - 4) * k;
        std::vector<std::string> lines;
        float ts = 11.f;
        for (; ts >= 4.f; ts -= 0.25f)
        {
            p.font(false, std::round(ts * k));
            lines = Wrap(p, desc, rw);
            if (lines.size() * kLine * std::round(ts * k) <= rh) break;
        }
        float px = std::round(ts * k);
        p.cv.text_align = canvas_ity::leftward;
        for (size_t i = 0; i < lines.size(); i++)
            OutlinedText(p, lines[i], rx, ry + kAscent * px + i * kLine * px, px, white, dark, 0.22f);
    }
    w = p.W; h = p.H;
    return p.pixels();
}

std::vector<u8> StatPanel(const WC09::StatPanelInfo& info, float k, int& w, int& h)
{
    int pw, ph;
    std::vector<u8> panel = TextPanel(info.Card, k, info.Bubble ? WC09::kPanelShortH : WC09::kPanelH, pw, ph);
    if (panel.empty()) return {};
    const int W = WC09::kPanelX + WC09::kPanelW, H = WC09::kStatsY + WC09::kStatsH - WC09::kPanelY;
    Painter p(W, H, k);
    // faint alpha over the panel area hides the game's own text there (as on the desktop)
    p.solid(canvas_ity::fill_style, C(0, 0, 0, 77));
    p.cv.fill_rectangle(WC09::kPanelX * k, 0, WC09::kPanelW * k, (float)ph);
    p.cv.draw_image(panel.data(), pw, ph, pw * 4, WC09::kPanelX * k, 0, (float)pw, (float)ph);
    const RGBA fill = C(14, 26, 56), edge = C(140, 190, 255, 230);
    if (info.Atk >= 0)
    {
        const float y0 = WC09::kStatsY - WC09::kPanelY;
        p.roundRect(13, y0 - 0.5f, 84, WC09::kStatsH + 1, 3);
        p.solid(canvas_ity::fill_style, fill); p.cv.fill();
        p.solid(canvas_ity::stroke_style, edge); p.strokeWidth(0.7f); p.cv.stroke();
        float px = std::round(10.5f * k);
        p.font(true, px);
        float cy = (y0 - 0.5f + (WC09::kStatsH + 1) / 2.f) * k + (kAscent - kDescent) / 2 * px;
        p.solid(canvas_ity::fill_style, C(255, 255, 255));
        p.cv.text_align = canvas_ity::rightward; p.cv.fill_text(std::to_string(info.Atk).c_str(), (14 + 31) * k, cy);
        p.cv.text_align = canvas_ity::leftward; p.cv.fill_text(std::to_string(info.Def).c_str(), 64 * k, cy);
    }
    if (info.Level > 0)
    {
        float px = std::round(8.5f * k);
        p.font(true, px);
        std::string num = std::to_string(info.Level);
        float tw = p.measure(num) / k;
        const float bh = 10.f, star = 7.5f, pad = 2.f;
        float bw = std::max(pad + star + 1.5f + tw + pad + 0.5f, info.Level * 6.4f + 2.f);
        float x1 = info.BadgeRight + 1.f, x0 = x1 - bw;
        float yc = info.BadgeMidY + 0.5f - WC09::kPanelY, y0 = yc - bh / 2;
        p.roundRect(x0, y0, bw, bh, 3.f);
        p.solid(canvas_ity::fill_style, fill); p.cv.fill();
        p.solid(canvas_ity::stroke_style, edge); p.strokeWidth(0.7f); p.cv.stroke();
        float gx = x0 + (bw - (star + 2.f + tw)) / 2;
        float cx = gx + star / 2, cy = yc, ro = star / 2, ri = ro * 0.45f;
        p.cv.begin_path();
        for (int i = 0; i < 10; i++)
        {
            float a = -3.14159265f / 2 + i * 3.14159265f / 5, r = (i & 1) ? ri : ro;
            float X = (cx + r * std::cos(a)) * k, Y = (cy + r * std::sin(a)) * k;
            if (i == 0) p.cv.move_to(X, Y); else p.cv.line_to(X, Y);
        }
        p.cv.close_path();
        p.vgrad(canvas_ity::fill_style, cy - ro, cy + ro, {{0.f, C(255, 236, 140)}, {1.f, C(240, 160, 30)}});
        p.cv.fill();
        p.solid(canvas_ity::stroke_style, C(120, 60, 0)); p.strokeWidth(0.5f); p.cv.stroke();
        p.solid(canvas_ity::fill_style, C(255, 255, 255));
        p.cv.text_align = canvas_ity::leftward;
        p.cv.fill_text(num.c_str(), (gx + star + 2.f) * k, yc * k + (kAscent - kDescent) / 2 * px);
    }
    w = p.W; h = p.H;
    return p.pixels();
}

std::vector<u8> LpPanel(int lp0, int lp1, int turn, int phase, bool turnRight, float k, int& w, int& h)
{
    const float W = WC09::kLpW, H = WC09::kLpH;
    Painter p(W, H, k);
    p.roundRect(0.5f, 0.5f, W - 1, H - 1, 6);
    p.vgrad(canvas_ity::fill_style, 0, H, {{0.f, C(30, 50, 100)}, {1.f, C(14, 24, 54)}});
    p.cv.fill();
    p.solid(canvas_ity::stroke_style, C(120, 175, 255, 230)); p.strokeWidth(0.8f); p.cv.stroke();

    const RGBA youC[3] = {C(0, 96, 220), C(40, 180, 255), C(150, 235, 255)};
    const RGBA oppC[3] = {C(170, 16, 48), C(240, 64, 90), C(255, 160, 172)};
    auto bar = [&](float x, float y, float bw, float bh, float ratio, bool fromLeft, const RGBA* cs) {
        p.roundRect(x, y, bw, bh, bh / 2);
        p.solid(canvas_ity::fill_style, C(6, 10, 26)); p.cv.fill();
        ratio = std::clamp(ratio, 0.f, 1.f);
        if (ratio > 0)
        {
            float fw = std::max(bh, bw * ratio);
            float fx = fromLeft ? x : x + bw - fw;
            p.roundRect(fx, y, fw, bh, bh / 2);
            if (fromLeft) p.hgrad(canvas_ity::fill_style, x, x + bw, {{0.f, cs[0]}, {0.55f, cs[1]}, {1.f, cs[2]}});
            else p.hgrad(canvas_ity::fill_style, x + bw, x, {{0.f, cs[0]}, {0.55f, cs[1]}, {1.f, cs[2]}});
            p.cv.fill();
            p.vgrad(canvas_ity::fill_style, y, y + bh, {{0.f, C(255, 255, 255, 120)}, {0.5f, C(255, 255, 255, 0)}, {1.f, C(255, 255, 255, 0)}});
            p.cv.fill();
        }
        p.roundRect(x, y, bw, bh, bh / 2);
        RGBA e = cs[1]; e.a = 170 / 255.f;
        p.solid(canvas_ity::stroke_style, e); p.strokeWidth(0.5f); p.cv.stroke();
    };
    auto plate = [&](float x, float y, float pw, float ph, const RGBA* cs) {
        p.roundRect(x, y, pw, ph, 3.5f);
        RGBA top = {cs[0].r / 2.5f, cs[0].g / 2.5f, cs[0].b / 2.5f, 1};
        p.vgrad(canvas_ity::fill_style, y, y + ph, {{0.f, top}, {1.f, C(6, 10, 26)}});
        p.cv.fill();
        p.solid(canvas_ity::stroke_style, cs[1]); p.strokeWidth(0.7f); p.cv.stroke();
    };
    // align: 0 left, 1 centre, 2 right
    auto text = [&](float x, float y, float tw, float th, const std::string& t, float pxDS, int align, RGBA col) {
        float px = std::max(1.f, std::round(pxDS * k));
        p.font(true, px);
        float wdt = p.measure(t);
        float X = align == 2 ? (x + tw) * k - wdt : align == 1 ? (x + tw / 2) * k - wdt / 2 : x * k;
        float Y = (y + th / 2) * k + (kAscent - kDescent) / 2 * px;
        p.cv.text_align = canvas_ity::leftward;
        OutlinedText(p, t, X, Y, px, col, C(6, 10, 28, 230), 0.22f);
    };
    const float p0x = 19.5f, p0y = 2.5f, pw = 37, ph = 12;
    const float p1x = W - 56.5f, p1y = H - 14.5f;
    plate(p0x, p0y, pw, ph, youC); plate(p1x, p1y, pw, ph, oppC);
    bar(p0x + pw + 3, 4.5f, W - 3 - (p0x + pw + 3), 8, lp0 / 8000.f, true, youC);
    bar(3, H - 12.5f, p1x - 3 - 3, 8, lp1 / 8000.f, false, oppC);
    text(p0x, p0y, pw, ph, std::to_string(lp0), 9.5f, 1, C(255, 255, 255));
    text(p1x, p1y, pw, ph, std::to_string(lp1), 9.5f, 1, C(255, 255, 255));
    text(5, 15, 36, 11, "YOU", 6.6f, 0, youC[2]);
    text(W - 41, 15, 36, 11, "OPPONENT", 6.6f, 2, oppC[2]);
    if (turn >= 0)
    {
        float cx = turnRight ? W - 17 : 3, cy = turnRight ? H - 15.5f : 1.5f;
        p.ellipse(cx, cy, 14, 14);
        p.solid(canvas_ity::fill_style, turnRight ? C(170, 40, 70) : C(44, 74, 150)); p.cv.fill();
        p.solid(canvas_ity::stroke_style, C(200, 225, 255)); p.strokeWidth(0.7f); p.cv.stroke();
        text(cx, cy, 14, 14, std::to_string(turn), turn >= 10 ? 7.f : 8.5f, 1, C(255, 255, 255));
    }
    static const char* const kNames[6] = {"Draw Phase", "Standby Phase", "Main Phase 1", "Battle Phase", "Main Phase 2", "End Phase"};
    if (phase >= 0 && phase < 6)
    {
        float x = W / 2 - 34, y = 15, bw = 68, bh = 11;
        p.roundRect(x, y, bw, bh, 5.5f);
        RGBA c0 = phase == 3 ? C(245, 95, 110) : C(80, 160, 255), c1 = phase == 3 ? C(175, 35, 60) : C(34, 90, 210);
        p.vgrad(canvas_ity::fill_style, y, y + bh, {{0.f, c0}, {1.f, c1}});
        p.cv.fill();
        p.solid(canvas_ity::stroke_style, C(220, 240, 255)); p.strokeWidth(0.6f); p.cv.stroke();
        text(x, y, bw, bh, kNames[phase], 7.4f, 1, C(255, 255, 255));
    }
    w = p.W; h = p.H;
    return p.pixels();
}

bool LoadImageFile(const std::string& base, std::vector<u8>& rgba, int& w, int& h)
{
    for (const char* ext : {".png", ".jpg", ".jpeg"})
    {
        std::string path = base + ext;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        int n = 0;
        unsigned char* data = stbi_load_from_file(f, &w, &h, &n, 4);
        fclose(f);
        if (!data) continue;
        rgba.assign(data, data + (size_t)w * h * 4);
        stbi_image_free(data);
        return true;
    }
    return false;
}

std::mutex RenderLock;   // canvas_ity is reentrant, but the fonts are shared
}

// panels are drawn at the screen's resolution (8x for everything was up to 16 times the pixels
// at 2x, drawn on the CPU)
static std::atomic<float> PanelK{4.f};
void SetPanelScale(int scale) { PanelK = (float)std::max(3, std::min(8, scale)); }

// every picture in the HD pack (path without extension), listed once in the background: missing
// pictures are then known at once instead of each costing a trip through the loader
namespace
{
std::mutex IndexLock;
std::shared_ptr<const std::unordered_set<std::string>> Index;
std::string IndexedFolder;

void ScanDir(const std::string& root, const std::string& rel, std::unordered_set<std::string>& out, int depth)
{
    DIR* d = opendir((root + "/" + rel).c_str());
    if (!d) return;
    while (dirent* e = readdir(d))
    {
        std::string n = e->d_name;
        if (n.empty() || n[0] == '.') continue;
        std::string r = rel.empty() ? n : rel + "/" + n;
        bool isDir = e->d_type == DT_DIR;
        if (e->d_type == DT_UNKNOWN) { struct stat st; isDir = stat((root + "/" + r).c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
        if (isDir) { if (depth < 4) ScanDir(root, r, out, depth + 1); continue; }
        size_t dot = r.rfind('.');
        size_t slash = r.rfind('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) out.insert(r.substr(0, dot));
    }
    closedir(d);
}
}

void SetupHDProvider(const std::string& folder, const std::string& fontDir)
{
    {
        std::lock_guard<std::mutex> lk(IndexLock);
        if (IndexedFolder != folder)
        {
            IndexedFolder = folder;
            Index.reset();
            std::thread([folder] {
                auto set = std::make_shared<std::unordered_set<std::string>>();
                ScanDir(folder, "", *set, 0);
                std::lock_guard<std::mutex> lk2(IndexLock);
                if (IndexedFolder == folder) Index = set;
            }).detach();
        }
    }
    if (!getenv("WC09_NOINDEX")) WC09::SetHDExistsCheck([](const std::string& name) -> int {
        static const char* const kMade[] = {"lppanel/", "statpanel/", "decktext/", "deckname/", "textpanel/", "cardimg/"};
        for (const char* m : kMade) if (name.compare(0, strlen(m), m) == 0) return -1;
        std::shared_ptr<const std::unordered_set<std::string>> idx;
        { std::lock_guard<std::mutex> lk(IndexLock); idx = Index; }
        if (!idx) return -1;
        if (idx->count(name)) return 1;
        // left-facing battle pictures can be mirrored from the right-facing ones
        if (name.compare(0, 14, "BattleMonsterL") == 0 && name.size() > 2 && name.compare(name.size() - 2, 2, "_l") == 0)
            return idx->count("BattleMonsterR" + name.substr(14, name.size() - 14 - 2) + "_r") ? 1 : 0;
        return 0;
    });
    if (FontMedium.empty()) FontMedium = ReadAll(fontDir + "/Inter-Medium.ttf");
    if (FontBold.empty()) FontBold = ReadAll(fontDir + "/Inter-Bold.ttf");
    std::string f = folder;
    WC09::SetHDImageProvider([f](const std::string& name, std::vector<u8>& rgba, int& w, int& h) {
        auto starts = [&](const char* p) { return name.compare(0, strlen(p), p) == 0; };
        if (starts("lppanel/"))
        {
            int lp0, lp1, turn, phase; bool right;
            if (!WC09::GetLpPanel(atoi(name.c_str() + 8), lp0, lp1, turn, phase, right)) return false;
            std::lock_guard<std::mutex> lk(RenderLock);
            rgba = LpPanel(lp0, lp1, turn, phase, right, PanelK, w, h);
            return !rgba.empty();
        }
        if (starts("statpanel/"))
        {
            WC09::StatPanelInfo info;
            if (!WC09::GetStatPanel(atoi(name.c_str() + 10), info)) return false;
            std::lock_guard<std::mutex> lk(RenderLock);
            rgba = StatPanel(info, PanelK, w, h);
            return !rgba.empty();
        }
        if (starts("decktext/") || starts("deckname/"))
        {
            std::lock_guard<std::mutex> lk(RenderLock);
            rgba = DeckText(atoi(name.c_str() + 9), PanelK, starts("deckname/"), w, h);
            return !rgba.empty();
        }
        if (starts("textpanel/"))
        {
            std::string id = name.substr(10);
            bool shortVer = !id.empty() && id.back() == 's';
            if (shortVer) id.pop_back();
            std::lock_guard<std::mutex> lk(RenderLock);
            rgba = TextPanel(atoi(id.c_str()), PanelK, shortVer ? WC09::kPanelShortH : WC09::kPanelH, w, h);
            return !rgba.empty();
        }
        if (f.empty()) return false;
        if (LoadImageFile(f + "/" + name, rgba, w, h)) return true;
        // left-facing battle images are mostly mirrors of the right-facing ones
        if (starts("BattleMonsterL") && name.size() > 2 && name.compare(name.size() - 2, 2, "_l") == 0)
        {
            std::string r = "BattleMonsterR" + name.substr(14, name.size() - 14 - 2) + "_r";
            if (LoadImageFile(f + "/" + r, rgba, w, h))
            {
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w / 2; x++)
                        std::swap(*(u32*)&rgba[((size_t)y * w + x) * 4], *(u32*)&rgba[((size_t)y * w + (w - 1 - x)) * 4]);
                return true;
            }
        }
        return false;
    });
}
}
