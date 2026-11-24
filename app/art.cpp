#include "art.hpp"

#include <QGuiApplication>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygonF>
#include <QSet>
#include <QtMath>
#include <cctype>
#include <cmath>
#include <vector>
#include <cstdlib>

#include "theme.hpp"

namespace art {
namespace {

// ── path reader ─────────────────────────────────────────────────
struct Reader {
    const char* s;
    void ws() { while (*s && (std::isspace(static_cast<unsigned char>(*s)) || *s == ',')) ++s; }
    bool number() {
        ws();
        return *s && (std::isdigit(static_cast<unsigned char>(*s)) || *s == '-' || *s == '+' || *s == '.');
    }
    double num() {
        ws();
        char* end = nullptr;
        double v = std::strtod(s, &end);
        s = end;
        return v;
    }
};

QHash<QString, QPixmap>& cache() {
    static QHash<QString, QPixmap> c;
    return c;
}

// Line icons on a 24×24 grid (original designs).
const QHash<QString, const char*>& icons() {
    static const QHash<QString, const char*> m = {
        {"crosshair", "O 12 12 7.5 M12 1.5 V7 M12 17 V22.5 M1.5 12 H7 M17 12 H22.5 O 12 12 0.9"},
        {"codes", "M3 7 H21 V10 C19.3 10 19.3 14 21 14 V17 H3 V14 C4.7 14 4.7 10 3 10 Z M9 8.5 V9.5 M9 11.5 V12.5 M9 14.5 V15.5"},
        {"rifle", "M2 10.5 H14 L15.5 9 H22 V11.5 H16 L15 13.5 H10.5 L9.5 16.5 H7 L7.8 13.5 H2 Z M8 10.5 V8.5 H12 V10.5"},
        {"chart", "M3 21 H21 M6 17 V11 M11 17 V6 M16 17 V13 M20 17 V8"},
        {"star", "M12 3 L14.6 8.6 L20.6 9.2 L16 13.2 L17.4 19.2 L12 16 L6.6 19.2 L8 13.2 L3.4 9.2 L9.4 8.6 Z"},
        {"users", "O 9 8 3.2 M2.5 20 C2.5 15.5 5.5 13.5 9 13.5 C12.5 13.5 15.5 15.5 15.5 20 M16 4.8 C18 5.3 19 6.8 19 8.2 "
                  "C19 9.8 18 11 16 11.5 M18 14 C20.3 14.7 21.5 16.6 21.5 20"},
        {"bot", "M5 9 H19 V19 H5 Z M12 9 V5.2 O 12 4 1.2 M9 13.5 V14.5 M15 13.5 V14.5 M9.5 17 H14.5 M2.5 13 V16 M21.5 13 V16"},
        {"image", "M3 5 H21 V19 H3 Z M3 16 L8.5 11 L13 15 L16 12.5 L21 17 O 16 8.5 1.6"},
        {"brush", "M14.5 3.5 L20.5 9.5 L11 19 L5 13 Z M5 13 C3 15 4 19 2.5 21.5 C6 21 9 20.5 11 19 M12 6 L18 12"},
        {"globe", "O 12 12 9 M3 12 H21 M12 3 C15 6 15.5 18 12 21 M12 3 C9 6 8.5 18 12 21 M4.5 7.5 H19.5 M4.5 16.5 H19.5"},
        {"megaphone", "M3 10 V14 H6.5 L14 19 V5 L6.5 10 Z M17 9.5 C18.3 10.8 18.3 13.2 17 14.5 M19.5 7 C22 9.5 22 14.5 19.5 17 "
                      "M6.5 14 L7.5 20 H10 L9.3 15.6"},
        {"shield", "M12 2.5 L20 5.5 V11 C20 16 16.5 19.8 12 21.5 C7.5 19.8 4 16 4 11 V5.5 Z M8.5 12 L11 14.5 L15.5 9.5"},
        {"pulse", "M2 12 H6 L9 4.5 L15 19.5 L18 12 H22"},
        {"search", "O 10.5 10.5 6.5 M15.5 15.5 L21 21"},
        {"play", "M7 4.5 L19.5 12 L7 19.5 Z"},
        {"stop", "M6 6 H18 V18 H6 Z"},
        {"refresh", "M20 11 C19.5 6.8 16 4 12 4 C8.6 4 5.7 6 4.5 9 M4 13 C4.5 17.2 8 20 12 20 C15.4 20 18.3 18 19.5 15 "
                    "M20 4 V9.5 H14.5 M4 20 V14.5 H9.5"},
        {"plus", "M12 5 V19 M5 12 H19"},
        {"trash", "M4 7 H20 M9 7 V4.5 H15 V7 M6.5 7 L7.5 20 H16.5 L17.5 7 M10 11 V16 M14 11 V16"},
        {"edit", "M4 20 L4.8 16 L15.5 5.3 C16.5 4.3 18 4.3 19 5.3 C20 6.3 20 7.8 19 8.8 L8.3 19.5 Z M14 7 L17.3 10.3"},
        {"download", "M12 3.5 V15 M7 10.5 L12 15.5 L17 10.5 M4 20 H20"},
        {"upload", "M12 15.5 V4 M7 9 L12 4 L17 9 M4 20 H20"},
        {"copy", "M8 8 H20 V20 H8 Z M16 8 V4 H4 V16 H8"},
        {"save", "M4 4 H17 L20 7 V20 H4 Z M7.5 4 V9 H15 V4 M7.5 20 V14 H16.5 V20"},
        {"link", "M10 14 C11.5 15.5 13.5 15.5 15 14 L18.5 10.5 C20 9 20 6.5 18.5 5.2 C17 3.8 14.8 3.8 13.4 5.2 L12 6.5 "
                 "M14 10 C12.5 8.5 10.5 8.5 9 10 L5.5 13.5 C4 15 4 17.5 5.5 18.8 C7 20.2 9.2 20.2 10.6 18.8 L12 17.5"},
        {"check", "M4.5 12.5 L9.5 17.5 L19.5 6.5"},
        {"close", "M6 6 L18 18 M18 6 L6 18"},
        {"menu", "M4 7 H20 M4 12 H20 M4 17 H20"},
        {"more", "O 5 12 1 O 12 12 1 O 19 12 1"},
        {"bell", "M6 16.5 V11 C6 7.5 8.5 5 12 5 C15.5 5 18 7.5 18 11 V16.5 L19.5 18 H4.5 Z "
                 "M10 20.5 C10.5 21.3 11.2 21.6 12 21.6 C12.8 21.6 13.5 21.3 14 20.5 M12 3 V5"},
        {"dogtag", "M7.5 3 H16.5 L19 6 V19 C19 20.2 18 21 17 21 H7 C6 21 5 20.2 5 19 V5.5 C5 4 6 3 7.5 3 Z O 12 6.5 1.2 "
                   "M8.5 11 H15.5 M8.5 14 H15.5 M8.5 17 H13"},
        {"bolt", "M13.5 2.5 L5 13.5 H11.5 L10.5 21.5 L19 10.5 H12.5 Z"},
        {"filter", "M3.5 5 H20.5 L14 12.5 V19 L10 21 V12.5 Z"},
        {"eye", "M2 12 C4.5 7.5 8 5.5 12 5.5 C16 5.5 19.5 7.5 22 12 C19.5 16.5 16 18.5 12 18.5 C8 18.5 4.5 16.5 2 12 Z O 12 12 3"},
        {"bandage", "M4.2 15.5 L15.5 4.2 C16.8 2.9 19 2.9 20.3 4.2 C21.6 5.5 21.6 7.7 20.3 9 L9 20.3 C7.7 21.6 5.5 21.6 4.2 20.3 "
                    "C2.9 19 2.9 16.8 4.2 15.5 Z M10 10 L14 14"},
        {"thumbup", "M7 10.5 V20.5 H4 V10.5 Z M7 10.5 L11 3.5 C12.5 3.5 13.5 4.5 13.5 6 L13 9.5 H18.5 C19.8 9.5 20.7 10.7 20.4 12 "
                    "L18.9 18.5 C18.6 19.7 17.6 20.5 16.4 20.5 H7"},
        {"thumbdown", "M7 13.5 V3.5 H4 V13.5 Z M7 13.5 L11 20.5 C12.5 20.5 13.5 19.5 13.5 18 L13 14.5 H18.5 C19.8 14.5 20.7 13.3 20.4 12 "
                      "L18.9 5.5 C18.6 4.3 17.6 3.5 16.4 3.5 H7"},
        {"send", "M21 3 L10 14 M21 3 L14.5 21 L10 14 L3 9.5 Z"},
        {"chevron-left", "M15 5 L8 12 L15 19"},
        {"chevron-right", "M9 5 L16 12 L9 19"},
        {"chevron-down", "M5 9 L12 16 L19 9"},
        {"grid", "M4 4 H10 V10 H4 Z M14 4 H20 V10 H14 Z M4 14 H10 V20 H4 Z M14 14 H20 V20 H14 Z"},
        {"trophy", "M7 4 H17 V9 C17 12 15 14 12 14 C9 14 7 12 7 9 Z M7 6 H4 C4 9 5.5 10.5 7.5 10.8 M17 6 H20 C20 9 18.5 10.5 16.5 10.8 "
                   "M12 14 V17.5 M9.5 17.5 H14.5 V20.5 H9.5 Z"},
        {"palette", "M12 3 C7 3 3 7 3 12 C3 17 7 21 12 21 C13.3 21 14 20.2 14 19.2 C14 18 13 17.6 13 16.5 C13 15.5 13.8 14.8 15 14.8 "
                    "H17 C19.3 14.8 21 13 21 10.8 C21 6.5 17 3 12 3 Z O 7.5 11.5 1.2 O 10 7.5 1.2 O 15 7.5 1.2 O 17.5 11 1.2"},
        {"sliders", "M4 6 H14 M18 6 H20 O 16 6 2 M4 12 H6 M10 12 H20 O 8 12 2 M4 18 H12 M16 18 H20 O 14 18 2"},
        {"flag", "M5 21 V4 M5 4 H17 L15 8 L17 12 H5"},
        {"clock", "O 12 12 9 M12 7 V12 L15.5 14"},
        {"target", "O 12 12 9 O 12 12 5 O 12 12 1.2"},
        {"layers", "M12 3 L21 8 L12 13 L3 8 Z M3 12.5 L12 17.5 L21 12.5 M3 17 L12 21.5 L21 17"},
        {"command", "M9 6 C9 4.3 7.7 3 6 3 C4.3 3 3 4.3 3 6 C3 7.7 4.3 9 6 9 H18 C19.7 9 21 7.7 21 6 C21 4.3 19.7 3 18 3 "
                    "C16.3 3 15 4.3 15 6 V18 C15 19.7 16.3 21 18 21 C19.7 21 21 19.7 21 18 C21 16.3 19.7 15 18 15 H6 "
                    "C4.3 15 3 16.3 3 18 C3 19.7 4.3 21 6 21 C7.7 21 9 19.7 9 18 V6"},
        {"info", "O 12 12 9 M12 11 V16.5 M12 7.6 V7.7"},
        {"key", "O 8 15 4 M11 12 L20 3 M17 6 L19.5 8.5 M15 8 L17.5 10.5"},
        {"warning", "M12 3 L22 20.5 H2 Z M12 9.5 V14 M12 17 V17.1"},
        {"folder", "M3 6 H9.5 L11.5 8 H21 V19 H3 Z"},
        {"lock", "M5 11 H19 V21 H5 Z M8 11 V7.5 C8 5.3 9.8 3.5 12 3.5 C14.2 3.5 16 5.3 16 7.5 V11 M12 15 V17"},
    };
    return m;
}
const QSet<QString>& filledIcons() {
    static const QSet<QString> f = {"play", "stop", "bolt", "more"};
    return f;
}

QPainterPath gearPath() {
    QPainterPath p;
    const int teeth = 8;
    QPolygonF poly;
    for (int i = 0; i < teeth; ++i) {
        double a = 2 * M_PI * i / teeth;
        double w = M_PI / teeth * 0.55;
        const double pts[4][2] = {{a - w * 1.4, 7.0}, {a - w * 0.8, 9.6}, {a + w * 0.8, 9.6}, {a + w * 1.4, 7.0}};
        for (const auto& pt : pts) poly << QPointF(12 + pt[1] * std::cos(pt[0]), 12 + pt[1] * std::sin(pt[0]));
    }
    poly << poly.first();
    p.addPolygon(poly);
    p.addEllipse(QPointF(12, 12), 3.2, 3.2);
    return p;
}

// Weapon-class outlines in a 200×60 box (original, simplified shapes).
const QHash<QString, const char*>& weapons() {
    static const QHash<QString, const char*> m = {
        {"ar", "M4 27 L28 23 L34 21 H62 V15 H98 V21 H140 L142 18 H160 V21 H184 L188 20 H196 V25 H188 L184 24 H160 V28 L150 31 "
               "H126 L114 33 H104 L97 52 L86 50 L92 33 H74 L70 46 H60 L62 33 H38 L10 40 L4 38 Z"},
        {"smg", "M14 26 H40 V20 H64 V16 H90 V20 H128 L132 22 H150 V27 H132 L128 29 H112 L108 31 H98 V50 H88 V31 H76 L72 44 H62 "
                "L64 31 H40 V30 H22 V38 H14 Z"},
        {"lmg", "M2 26 L26 21 H48 V14 H90 V20 H150 L154 18 H190 V24 H154 L150 27 H120 L118 31 H96 V44 H70 V31 H64 L60 44 H50 L52 31 "
                "H30 L6 38 Z M146 27 L138 50 H143 L151 28 Z M156 27 L166 50 H161 L152 28 Z"},
        {"sniper", "M2 28 L24 22 H44 L52 26 H120 L124 24 H198 V28 H124 L120 30 H92 L86 34 H70 L66 44 H56 L58 34 H40 L30 38 H14 L2 36 Z "
                   "M52 14 H66 V12 H104 V14 H118 V20 H104 V22 H66 V20 H52 Z M150 28 L142 48 H146.5 L154.5 29 Z"},
        {"shotgun", "M4 30 L30 24 H64 V22 H190 V27 H150 V31 H96 V33 H66 L60 46 H50 L54 33 H36 L8 40 Z M100 32 H146 V36 H100 Z"},
        {"marksman", "M2 27 L26 22 H46 V21 H58 V14 H100 V21 H146 L150 19 H194 V24 H150 L146 26 H116 L112 30 H100 L96 44 H86 L90 30 "
                     "H72 L68 44 H58 L60 30 H40 L8 38 L2 36 Z"},
        {"pistol", "M60 16 H140 L142 20 V27 H104 L100 31 H94 L88 52 H70 L76 27 H60 Z M94 31 L92 38 C98 38 100 34 100 31 Z"},
        {"launcher", "M10 22 H170 L190 18 V40 L170 36 H10 Z M60 36 V48 H70 V36 Z M100 36 V50 H110 V36 Z M70 14 H96 V22 H70 Z"},
        {"knife", "M20 26 H70 V34 H20 Z M70 22 H76 V38 H70 Z M76 25 H170 C180 25 188 28 194 31 C186 34 176 35 166 35 H76 Z"},
    };
    return m;
}

// ── noise for the patterns ──────────────────────────────────────
inline double hash2(int x, int y, quint32 seed) {
    quint32 h = static_cast<quint32>(x) * 374761393u + static_cast<quint32>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffff) / double(0x1000000);
}
inline double smooth(double t) { return t * t * (3 - 2 * t); }
double vnoise(double x, double y, quint32 seed) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    double fx = smooth(x - xi), fy = smooth(y - yi);
    double a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
double fbm(double x, double y, quint32 seed) {
    double v = 0, amp = 0.55, f = 1;
    for (int o = 0; o < 4; ++o) {
        v += amp * vnoise(x * f, y * f, seed + o * 101);
        f *= 2.03;
        amp *= 0.5;
    }
    return v / 1.03;
}
QColor mix(const QColor& a, const QColor& b, double t, int alpha = 255) {
    return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t), alpha);
}

void paintDigital(QPainter& p, const QSize& sz, const Palette& pal, int intensity, quint32 seed, int cell, double scale) {
    const int alpha = 40 + intensity * 2;
    const QColor tones[3] = {mix(pal.surface, pal.surface2, 0.9, alpha), mix(pal.surface2, pal.border, 0.85, alpha),
                             mix(pal.border, pal.accent, 0.18, alpha)};
    for (int y = 0; y < sz.height(); y += cell)
        for (int x = 0; x < sz.width(); x += cell) {
            double n = fbm(x * scale, y * scale, seed);
            int t = n < 0.42 ? -1 : n < 0.52 ? 0 : n < 0.62 ? 1 : 2;
            if (t >= 0) p.fillRect(x, y, cell, cell, tones[t]);
        }
}

void paintTopo(QPainter& p, const QSize& sz, const Palette& pal, int intensity, quint32 seed) {
    const double step = 6, scale = 0.011;
    const int cols = sz.width() / int(step) + 2, rows = sz.height() / int(step) + 2;
    std::vector<double> f(size_t(cols * rows));
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) f[size_t(j * cols + i)] = fbm(i * step * scale, j * step * scale, seed);
    QColor line = mix(pal.border, pal.accent, 0.25, 50 + intensity * 2);
    QColor major = mix(pal.border, pal.accent, 0.55, 60 + intensity * 2);
    p.setRenderHint(QPainter::Antialiasing);
    for (int k = 0; k < 16; ++k) {
        const double lv = 0.2 + k * 0.045;
        p.setPen(QPen(k % 4 == 0 ? major : line, k % 4 == 0 ? 1.3 : 0.8));
        for (int j = 0; j + 1 < rows; ++j)
            for (int i = 0; i + 1 < cols; ++i) {  // marching squares
                double a = f[size_t(j * cols + i)], b = f[size_t(j * cols + i + 1)], c = f[size_t((j + 1) * cols + i + 1)],
                       d = f[size_t((j + 1) * cols + i)];
                int idx = (a > lv) | ((b > lv) << 1) | ((c > lv) << 2) | ((d > lv) << 3);
                if (idx == 0 || idx == 15) continue;
                double x = i * step, y = j * step;
                auto lerp = [&](double v1, double v2) { return (lv - v1) / (v2 - v1 + 1e-9); };
                QPointF top(x + step * lerp(a, b), y), right(x + step, y + step * lerp(b, c)), bottom(x + step * lerp(d, c), y + step),
                    left(x, y + step * lerp(a, d));
                auto seg = [&](QPointF u, QPointF v) { p.drawLine(u, v); };
                switch (idx) {
                    case 1: case 14: seg(left, top); break;
                    case 2: case 13: seg(top, right); break;
                    case 3: case 12: seg(left, right); break;
                    case 4: case 11: seg(right, bottom); break;
                    case 6: case 9: seg(top, bottom); break;
                    case 7: case 8: seg(left, bottom); break;
                    case 5: seg(left, top); seg(right, bottom); break;
                    case 10: seg(top, right); seg(left, bottom); break;
                }
            }
    }
}

void paintGrid(QPainter& p, const QSize& sz, const Palette& pal, int intensity) {
    QColor line = mix(pal.border, pal.accent, 0.15, 30 + intensity);
    QColor cross = mix(pal.border, pal.accent, 0.5, 70 + intensity * 2);
    p.setPen(QPen(line, 1));
    for (int x = 0; x < sz.width(); x += 24) p.drawLine(x, 0, x, sz.height());
    for (int y = 0; y < sz.height(); y += 24) p.drawLine(0, y, sz.width(), y);
    p.setPen(QPen(cross, 1));
    for (int x = 0; x < sz.width(); x += 96)
        for (int y = 0; y < sz.height(); y += 96) {
            p.drawLine(x - 4, y, x + 4, y);
            p.drawLine(x, y - 4, x, y + 4);
        }
    QColor scan = pal.text;
    scan.setAlpha(6 + intensity / 10);
    for (int y = 0; y < sz.height(); y += 3) p.fillRect(0, y, sz.width(), 1, scan);
}
}  // namespace

// ── public ──────────────────────────────────────────────────────
QPainterPath path(const char* d) {
    QPainterPath out;
    Reader r{d};
    QPointF cur, start;
    char cmd = 0;
    while (true) {
        r.ws();
        if (!*r.s) break;
        if (std::isalpha(static_cast<unsigned char>(*r.s))) cmd = *r.s++;
        else if (!cmd) break;
        const bool rel = std::islower(static_cast<unsigned char>(cmd)) && cmd != 'o';
        const QPointF o = rel ? cur : QPointF();
        switch (std::toupper(static_cast<unsigned char>(cmd))) {
            case 'M': {
                double x = r.num(), y = r.num();
                cur = start = o + QPointF(x, y);
                out.moveTo(cur);
                cmd = rel ? 'l' : 'L';  // further pairs are line-tos
                break;
            }
            case 'L': { double x = r.num(), y = r.num(); cur = o + QPointF(x, y); out.lineTo(cur); break; }
            case 'H': { double x = r.num(); cur.setX(rel ? cur.x() + x : x); out.lineTo(cur); break; }
            case 'V': { double y = r.num(); cur.setY(rel ? cur.y() + y : y); out.lineTo(cur); break; }
            case 'C': {
                double a = r.num(), b = r.num(), c = r.num(), d2 = r.num(), e = r.num(), f = r.num();
                out.cubicTo(o + QPointF(a, b), o + QPointF(c, d2), o + QPointF(e, f));
                cur = o + QPointF(e, f);
                break;
            }
            case 'Q': {
                double a = r.num(), b = r.num(), c = r.num(), d2 = r.num();
                out.quadTo(o + QPointF(a, b), o + QPointF(c, d2));
                cur = o + QPointF(c, d2);
                break;
            }
            case 'Z': out.closeSubpath(); cur = start; cmd = 0; break;
            case 'O': {
                double cx = r.num(), cy = r.num(), rad = r.num();
                out.addEllipse(QPointF(cx, cy), rad, rad);
                cur = QPointF(cx, cy);
                break;
            }
            default: return out;
        }
        if (cmd && !r.number() && !std::isalpha(static_cast<unsigned char>(*r.s)) && *r.s) break;  // garbage
    }
    return out;
}

bool hasIcon(const QString& name) { return name == "gear" || icons().contains(name); }

void drawIcon(QPainter& p, const QString& name, const QRectF& box, const QColor& color, qreal stroke) {
    QPainterPath pp;
    if (name == "gear") pp = gearPath();
    else if (auto it = icons().find(name); it != icons().end()) pp = path(it.value());
    else return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = std::min(box.width(), box.height()) / 24.0;
    p.translate(box.center().x() - 12 * s, box.center().y() - 12 * s);
    p.scale(s, s);
    QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(filledIcons().contains(name) ? QBrush(color) : Qt::NoBrush);
    p.drawPath(pp);
    p.restore();
}

QPixmap iconPixmap(const QString& name, const QColor& color, int size, qreal dpr) {
    const QString key = QStringLiteral("i|%1|%2|%3|%4").arg(name, color.name(QColor::HexArgb)).arg(size).arg(dpr);
    if (auto it = cache().find(key); it != cache().end()) return it.value();
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    drawIcon(p, name, QRectF(0, 0, size, size), color, size <= 18 ? 2.0 : 1.8);
    p.end();
    cache().insert(key, pm);
    return pm;
}

QIcon icon(const QString& name, const QColor& color) {
    QIcon ic;
    if (!hasIcon(name)) return ic;
    for (int sz : {16, 18, 20, 24, 32, 48})
        for (qreal dpr : {1.0, 2.0}) ic.addPixmap(iconPixmap(name, color, sz, dpr));
    QColor dim = color;
    dim.setAlpha(110);
    for (int sz : {16, 20, 24}) ic.addPixmap(iconPixmap(name, dim, sz, 2.0), QIcon::Disabled);
    return ic;
}

QString iconForPrefix(const QString& text, QString* rest) {
    static const QList<QPair<QString, QString>> map = {
        {"📊", "chart"}, {"🩹", "bandage"}, {"⟳", "refresh"}, {"＋", "plus"}, {"⚙", "gear"}, {"🔍", "search"},
        {"🔬", "search"}, {"🌐", "globe"}, {"🤖", "bot"}, {"🖼", "image"}, {"🎨", "palette"}, {"📣", "megaphone"},
        {"✔", "check"}, {"👍", "thumbup"}, {"👎", "thumbdown"}, {"▶", "play"}, {"■", "stop"}, {"⛔", "warning"},
        {"⤓", "download"}, {"⬇", "download"}, {"⬆", "upload"}, {"🗑", "trash"}, {"✎", "edit"}, {"📋", "copy"},
        {"💾", "save"}, {"🔗", "link"}, {"📤", "send"}, {"📎", "link"}, {"👁", "eye"}, {"🆕", "bolt"}, {"⭐", "star"},
        {"🔫", "rifle"}, {"🧰", "layers"}, {"👥", "users"}, {"🎯", "crosshair"}, {"🛡", "shield"}, {"📜", "pulse"},
        {"🔔", "bell"}, {"🏆", "trophy"}, {"⚡", "bolt"}, {"📁", "folder"}, {"🔑", "key"}, {"⋯", "more"}};
    for (const auto& [pre, name] : map) {
        if (!text.startsWith(pre)) continue;
        QString r = text.mid(pre.size());
        if (r.startsWith(QChar(0xFE0F))) r = r.mid(1);  // emoji variation selector
        if (rest) *rest = r.trimmed();
        return name;
    }
    if (rest) *rest = text;
    return {};
}

QPainterPath weapon(const QString& cls) {
    auto it = weapons().find(cls);
    return path(it == weapons().end() ? weapons().value("ar") : it.value());
}

void drawWeapon(QPainter& p, const QString& cls, const QRectF& box, const QColor& fill, const QColor& edge) {
    QPainterPath w = weapon(cls);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = std::min(box.width() / 200.0, box.height() / 60.0);
    p.translate(box.center().x() - 100 * s, box.center().y() - 30 * s);
    p.scale(s, s);
    p.setPen(edge.isValid() ? QPen(edge, 1.2 / s) : QPen(Qt::NoPen));
    p.setBrush(fill);
    p.drawPath(w);
    p.restore();
}

QStringList patternKinds() { return {"digital", "woodland", "topo", "grid", "none"}; }

QPixmap pattern(const QString& kind, const QSize& size, const Palette& pal, int intensity, quint32 seed, qreal dpr) {
    if (size.isEmpty() || kind == "none") return {};
    const QString key = QStringLiteral("p|%1|%2x%3|%4|%5|%6|%7").arg(kind).arg(size.width()).arg(size.height())
                            .arg(pal.name).arg(intensity).arg(seed).arg(dpr);
    if (auto it = cache().find(key); it != cache().end()) return it.value();
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (kind == "digital") paintDigital(p, size, pal, intensity, seed, 5, 0.035);
    else if (kind == "woodland") paintDigital(p, size, pal, intensity, seed, 2, 0.012);
    else if (kind == "topo") paintTopo(p, size, pal, intensity, seed);
    else if (kind == "grid") paintGrid(p, size, pal, intensity);
    p.end();
    if (cache().size() > 400) cache().clear();
    cache().insert(key, pm);
    return pm;
}

void drawCorners(QPainter& p, const QRectF& r, const QColor& c, qreal len, qreal width) {
    p.save();
    p.setPen(QPen(c, width, Qt::SolidLine, Qt::SquareCap));
    const qreal l = r.left(), t = r.top(), rt = r.right(), b = r.bottom();
    p.drawLine(QPointF(l, t + len), QPointF(l, t)); p.drawLine(QPointF(l, t), QPointF(l + len, t));
    p.drawLine(QPointF(rt - len, t), QPointF(rt, t)); p.drawLine(QPointF(rt, t), QPointF(rt, t + len));
    p.drawLine(QPointF(l, b - len), QPointF(l, b)); p.drawLine(QPointF(l, b), QPointF(l + len, b));
    p.drawLine(QPointF(rt - len, b), QPointF(rt, b)); p.drawLine(QPointF(rt, b), QPointF(rt, b - len));
    p.restore();
}

void drawChevrons(QPainter& p, const QRectF& box, const QColor& c, int n) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const qreal h = box.height() / (n + 0.6), w = box.width();
    for (int i = 0; i < n; ++i) {
        qreal y = box.top() + i * h;
        QPolygonF poly;
        poly << QPointF(box.left(), y + h * 0.75) << QPointF(box.left() + w / 2, y + h * 0.2) << QPointF(box.right(), y + h * 0.75)
             << QPointF(box.right(), y + h * 1.15) << QPointF(box.left() + w / 2, y + h * 0.6) << QPointF(box.left(), y + h * 1.15);
        p.drawPolygon(poly);
    }
    p.restore();
}

void drawEmblem(QPainter& p, const QRectF& box, const Palette& pal) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = std::min(box.width(), box.height());
    const QPointF c = box.center();
    QPolygonF hex;
    for (int i = 0; i < 6; ++i) {
        double a = M_PI / 6 + i * M_PI / 3;
        hex << c + QPointF(std::cos(a) * s * 0.47, std::sin(a) * s * 0.47);
    }
    QLinearGradient g(box.topLeft(), box.bottomRight());
    g.setColorAt(0, pal.accent.lighter(115));
    g.setColorAt(1, pal.accent.darker(135));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawPolygon(hex);
    QPolygonF inner;
    for (const auto& pt : hex) inner << c + (pt - c) * 0.8;
    p.setBrush(pal.appbar);
    p.drawPolygon(inner);
    QPen pen(pal.accent, s * 0.06, Qt::SolidLine, Qt::FlatCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, s * 0.19, s * 0.19);
    const qreal a = s * 0.08, b = s * 0.3;
    p.drawLine(c + QPointF(0, -b), c + QPointF(0, -a));
    p.drawLine(c + QPointF(0, a), c + QPointF(0, b));
    p.drawLine(c + QPointF(-b, 0), c + QPointF(-a, 0));
    p.drawLine(c + QPointF(a, 0), c + QPointF(b, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(pal.accent);
    p.drawEllipse(c, s * 0.035, s * 0.035);
    p.restore();
}

QIcon emblemIcon(const Palette& pal) {
    QIcon ic;
    for (int sz : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        drawEmblem(p, QRectF(0, 0, sz, sz), pal);
        p.end();
        ic.addPixmap(pm);
    }
    return ic;
}

void clearCache() { cache().clear(); }

}  // namespace art
