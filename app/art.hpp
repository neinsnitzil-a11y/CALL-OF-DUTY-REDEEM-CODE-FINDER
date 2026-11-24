// Original artwork drawn in code: the icon set, weapon-class outlines, camo / topographic / HUD-grid
// backgrounds, the app emblem and tactical corner brackets. Nothing here is copied from a game:
// no logos, renders or screenshots, so the app can be shared freely.
#pragma once
#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

struct Palette;
class QPainter;

namespace art {

// A tiny SVG path reader: M L H V C Q Z (absolute and relative) plus "O cx cy r" for circles.
QPainterPath path(const char* d);

// 24×24 line icons. Names: crosshair codes rifle chart star users bot image brush globe megaphone shield
// pulse gear search play stop refresh plus trash edit download upload copy save link check close menu
// bell dogtag bolt filter eye bandage thumbup thumbdown send chevron-left chevron-right grid trophy
// palette sliders flag clock target layers command
QIcon icon(const QString& name, const QColor& color);  // tinted; sharp on high-DPI
QPixmap iconPixmap(const QString& name, const QColor& color, int size, qreal dpr = 1.0);
void drawIcon(QPainter& p, const QString& name, const QRectF& box, const QColor& color, qreal stroke = 1.8);
bool hasIcon(const QString& name);
// Maps a leading emoji/symbol (as the older pages use: "📊 Research") to an icon name, or "".
QString iconForPrefix(const QString& text, QString* rest);

// Weapon-class outlines in a 200×60 box: ar smg lmg sniper shotgun marksman pistol launcher knife
QPainterPath weapon(const QString& cls);
void drawWeapon(QPainter& p, const QString& cls, const QRectF& box, const QColor& fill, const QColor& edge = QColor());

// Background patterns: digital (pixel camo), woodland, topo (contour map), grid (HUD), none.
// Cached per theme, size and seed. intensity 0–100.
QPixmap pattern(const QString& kind, const QSize& size, const Palette& p, int intensity = 50, quint32 seed = 7, qreal dpr = 1.0);
QStringList patternKinds();

// The app emblem: a crosshair in a hexagon with chevrons.
void drawEmblem(QPainter& p, const QRectF& box, const Palette& pal);
QIcon emblemIcon(const Palette& pal);

// Tactical corner brackets around a rectangle.
void drawCorners(QPainter& p, const QRectF& r, const QColor& c, qreal len = 10, qreal width = 2);
// Rank-style chevrons (n stacked), pointing up.
void drawChevrons(QPainter& p, const QRectF& box, const QColor& c, int n = 3);

void clearCache();  // call after a theme change
}  // namespace art
