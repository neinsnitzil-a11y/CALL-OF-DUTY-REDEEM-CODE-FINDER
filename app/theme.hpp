// Themes: six built-in tactical looks plus your own (saved as JSON in <data>/themes, shareable).
#pragma once
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct Palette {
    QString name, label;
    QColor bg, surface, surface2, border, text, muted, accent, accentText, hover, select, nav, appbar;
    QColor green, yellow, red, blue, grey, purple;
    bool mono = false;          // terminal-style monospace everywhere
    QString pattern = "digital";  // digital | woodland | topo | grid | none (art::patternKinds)
    int intensity = 50;         // pattern strength 0–100
    int radius = 8;             // corner radius in px (0–16)
    bool uppercase = true;      // UPPERCASE page titles, like a game menu
    int fontScale = 100;        // 85–130 %
    QString weapon = "ar";      // outline on the page banners (art::weapon) or "none"
    bool builtin = true;
};

namespace theme {
void init(const QString& userThemesDir);  // call once before apply(); loads your saved themes
Palette get(const QString& name);         // unknown → the default (Warzone); "dark"/"light" map to Warzone/Arctic
const Palette& current();
QStringList names();                      // built-ins first, then yours
void apply(const QString& name);          // restyles the whole application
void applyPalette(const Palette& p);      // live preview without saving
QColor statusColor(const QString& status);
QColor verdictColor(const QString& verdict);
QString uiFont();
QString monoFont();
QString headingFont();                    // condensed display face for titles and numbers

// your own themes
QJsonObject toJson(const Palette& p);
bool fromJson(const QJsonObject& o, Palette* out, QString* error = nullptr);
QString saveUser(Palette p, QString* error = nullptr);  // returns the saved name ("" on error)
bool removeUser(const QString& name);
bool isBuiltin(const QString& name);
QString importFile(const QString& path, QString* error = nullptr);  // returns the theme's name
bool exportFile(const QString& name, const QString& path, QString* error = nullptr);
QStringList colorKeys();                  // "bg", "surface", … in editor order
QColor* colorRef(Palette& p, const QString& key);
QString colorLabel(const QString& key);
}  // namespace theme
