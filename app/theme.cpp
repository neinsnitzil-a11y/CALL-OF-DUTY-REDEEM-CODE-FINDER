#include "theme.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QHash>
#include <algorithm>
#include <initializer_list>
#include <vector>

#include "art.hpp"

namespace {
struct Spec {
    const char *name, *label, *bg, *surface, *surface2, *border, *text, *muted, *accent, *accentText, *hover, *select, *nav,
        *appbar, *green, *yellow, *red, *blue, *grey, *purple, *pattern, *weapon;
    int intensity, radius;
    bool mono, uppercase;
};

// Original colour schemes inspired by military kit: gunmetal, blackout, night vision, desert, arctic, terminal.
const Spec kBuiltins[] = {
    {"warzone", "Warzone", "#0b0d10", "#13161b", "#1a1e25", "#262b34", "#e8eaed", "#8b93a1", "#ff7a1a", "#140a02", "#1f242c",
     "#3a2616", "#090b0d", "#0d1014", "#3ddc84", "#ffc53d", "#ff4d4f", "#4da3ff", "#6b7280", "#a78bfa", "digital", "ar", 55, 6, false, true},
    {"blackops", "Black Ops", "#050505", "#0e0e0f", "#151517", "#232326", "#f2f2f2", "#8a8a90", "#ff3b1f", "#ffffff", "#1a1a1d",
     "#3a130d", "#030303", "#080808", "#32d583", "#fdb022", "#f04438", "#53b1fd", "#667085", "#9e77ed", "grid", "smg", 45, 2, false, true},
    {"ghost", "Ghost (night vision)", "#070b08", "#0d140f", "#121b14", "#1d2b20", "#d7f5dc", "#7fa287", "#8cff5a", "#061006", "#142019",
     "#1d3a1f", "#050806", "#080d09", "#8cff5a", "#e6ff5a", "#ff5a5a", "#5ad2ff", "#5b6f60", "#b98cff", "topo", "sniper", 55, 6, false, true},
    {"desert", "Desert ops", "#12100c", "#1b1812", "#231f17", "#3a3224", "#f1e9da", "#a89a80", "#d9a54a", "#1a1307", "#2a251b",
     "#3b301c", "#0e0c09", "#15120d", "#8bc34a", "#f2c94c", "#e5533d", "#6fa8dc", "#7d7260", "#b39ddb", "woodland", "marksman", 50, 8, false, true},
    {"arctic", "Arctic (light)", "#eef1f4", "#ffffff", "#f5f7f9", "#d6dce3", "#141a21", "#5f6b7a", "#d9480f", "#ffffff", "#e9edf1",
     "#fde3d3", "#e5e9ee", "#ffffff", "#15803d", "#b45309", "#c62828", "#1d4ed8", "#8a94a3", "#6d28d9", "topo", "ar", 35, 8, false, true},
    {"hacker", "Terminal", "#000000", "#03110a", "#06190e", "#0f3d1a", "#33ff66", "#1f9e46", "#00ff66", "#000000", "#06240f",
     "#004d1a", "#000000", "#000000", "#33ff66", "#d4ff33", "#ff3355", "#33ccff", "#2f7a40", "#66ffcc", "grid", "pistol", 60, 0, true, true},
};

Palette fromSpec(const Spec& s) {
    Palette p;
    p.name = s.name; p.label = s.label;
    p.bg = QColor(s.bg); p.surface = QColor(s.surface); p.surface2 = QColor(s.surface2); p.border = QColor(s.border);
    p.text = QColor(s.text); p.muted = QColor(s.muted); p.accent = QColor(s.accent); p.accentText = QColor(s.accentText);
    p.hover = QColor(s.hover); p.select = QColor(s.select); p.nav = QColor(s.nav); p.appbar = QColor(s.appbar);
    p.green = QColor(s.green); p.yellow = QColor(s.yellow); p.red = QColor(s.red); p.blue = QColor(s.blue);
    p.grey = QColor(s.grey); p.purple = QColor(s.purple);
    p.mono = s.mono; p.pattern = s.pattern; p.weapon = s.weapon; p.intensity = s.intensity; p.radius = s.radius;
    p.uppercase = s.uppercase; p.builtin = true;
    return p;
}

std::vector<Palette>& builtins() {
    static std::vector<Palette> v = [] {
        std::vector<Palette> out;
        for (const auto& s : kBuiltins) out.push_back(fromSpec(s));
        return out;
    }();
    return v;
}
std::vector<Palette> g_user;
QString g_userDir;
Palette g_cur = fromSpec(kBuiltins[0]);

QString slug(const QString& label) {
    QString s = label.toLower();
    s.replace(QRegularExpression("[^a-z0-9]+"), "-");
    s.remove(QRegularExpression("^-+|-+$"));
    return s.isEmpty() ? QStringLiteral("my-theme") : s.left(40);
}

void loadUser() {
    g_user.clear();
    if (g_userDir.isEmpty()) return;
    QDir d(g_userDir);
    for (const QFileInfo& fi : d.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) continue;
        Palette p;
        if (!theme::fromJson(QJsonDocument::fromJson(f.readAll()).object(), &p)) continue;
        p.name = fi.completeBaseName();
        p.builtin = false;
        if (theme::isBuiltin(p.name)) continue;
        g_user.push_back(p);
    }
}

QString rgba(const QColor& c, int a) { return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a); }

QString css(const Palette& p) {
    auto c = [](const QColor& x) { return x.name(QColor::HexRgb); };
    const int r = std::clamp(p.radius, 0, 16), rs = std::max(0, r - 2), rl = r + 2;
    const double fs = std::clamp(p.fontScale, 85, 130) / 100.0;
    auto pt = [fs](double v) { return QString::number(v * fs, 'f', 1) + "pt"; };
    QString s = QStringLiteral(R"(
* { font-family: "%FONT%"; font-size: %F10%; }
QWidget { background: %BG%; color: %TEXT%; }
QToolTip { background: %SURF2%; color: %TEXT%; border: 1px solid %BORDER%; padding: 6px 9px; border-radius: %RS%px; }

/* ── app bar ── */
QFrame#AppBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %APPBAR%, stop:1 %BG%); border-bottom: 1px solid %BORDER%; }
QFrame#AppBar QLabel { background: transparent; }
QLabel#AppTitle { font-family: "%HEAD%"; font-size: %F15%; font-weight: 700; }
QLabel#AppSub, QLabel#Muted { color: %MUTED%; font-size: %F9%; background: transparent; }
QPushButton#Omni { background: %SURF%; border: 1px solid %BORDER%; border-radius: %RL%px; padding: 7px 14px; color: %MUTED%;
  font-weight: 400; text-align: left; min-width: 300px; }
QPushButton#Omni:hover { border-color: %ACCENT%; color: %TEXT%; }
QToolButton#ThemeBtn, QToolButton#MoreBtn { padding: 7px; }
QFrame#RailFooter { background: transparent; border-top: 1px solid %BORDER%; }
QLabel#Pill { background: %SURF2%; border: 1px solid %BORDER%; border-radius: 12px; padding: 4px 12px; color: %TEXT%; font-size: %F9%; font-weight: 600; }
QLabel#Pill[state="on"] { border-color: %GREEN%; color: %GREEN%; background: %GREENSOFT%; }
QLabel#Pill[state="busy"] { border-color: %ACCENT%; color: %ACCENT%; background: %ACCENTSOFT%; }
QLabel#Pill[state="off"] { color: %MUTED%; }
QLabel#Pill[state="bad"] { border-color: %RED%; color: %RED%; background: %REDSOFT%; }

/* ── navigation rail ── */
QFrame#NavRail { background: %NAV%; border-right: 1px solid %BORDER%; }
QFrame#NavRail QLabel { background: transparent; }
QListWidget#Nav { background: transparent; border: none; outline: 0; padding: 4px 10px; }
QListWidget#Nav::item { padding: 8px 10px; border-radius: %RS%px; margin: 1px 0; color: %MUTED%; border-left: 3px solid transparent; }
QListWidget#Nav::item:hover { background: %HOVER%; color: %TEXT%; }
QListWidget#Nav::item:selected { background: %ACCENTSOFT%; color: %TEXT%; border-left: 3px solid %ACCENT%; font-weight: 600; }
QListWidget#Nav::item:disabled { color: %MUTED%; font-family: "%HEAD%"; font-size: %F8%; font-weight: 700; padding-top: 16px; padding-bottom: 4px; background: transparent; border: none; }
QToolButton#NavToggle { color: %MUTED%; }

/* ── cards, banners, panels ── */
QFrame#Card, QFrame#Detail { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %SURF2%, stop:1 %SURF%); border: 1px solid %BORDER%; border-radius: %RL%px; }
QFrame#Card QLabel, QFrame#Detail QLabel { background: transparent; }
QLabel#CardNum { font-family: "%HEAD%"; font-size: %F24%; font-weight: 700; }
QLabel#CardLbl { color: %MUTED%; font-size: %F8%; font-weight: 700; }
QLabel#PageTitle { font-family: "%HEAD%"; font-size: %F20%; font-weight: 700; background: transparent; }
QLabel#PageHint { color: %MUTED%; background: transparent; }
QLabel#Kicker { color: %ACCENT%; font-family: "%HEAD%"; font-size: %F8%; font-weight: 700; background: transparent; }
QLabel#SectionTitle { font-family: "%HEAD%"; font-size: %F11%; font-weight: 700; background: transparent; }
QLabel#CodeBig { font-family: "%MONO%"; font-size: %F20%; font-weight: 700; color: %ACCENT%; }
QFrame#StatusBar { background: %APPBAR%; border-top: 1px solid %BORDER%; }
QFrame#StatusBar QLabel { background: transparent; color: %MUTED%; font-size: %F9%; }

/* ── buttons ── */
QPushButton { background: %SURF2%; border: 1px solid %BORDER%; border-radius: %RS%px; padding: 7px 15px; color: %TEXT%; font-weight: 600; min-height: 18px; }
QPushButton:hover { background: %HOVER%; border-color: %ACCENTMID%; }
QPushButton:pressed { background: %SELECT%; }
QPushButton:focus { border-color: %ACCENT%; }
QPushButton:disabled { color: %MUTED%; border-color: %BORDER%; background: %BG%; }
QPushButton[kind="accent"] { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %ACCENTHOVER%, stop:1 %ACCENT%); color: %ACCENTTEXT%; border: 1px solid %ACCENT%; font-weight: 700; }
QPushButton[kind="accent"]:hover { background: %ACCENTHOVER%; }
QPushButton[kind="accent"]:disabled { background: %SURF2%; color: %MUTED%; border-color: %BORDER%; }
QPushButton[kind="ghost"] { background: transparent; border: 1px solid transparent; }
QPushButton[kind="ghost"]:hover { background: %HOVER%; border-color: %BORDER%; }
QPushButton[kind="danger"] { background: transparent; color: %RED%; border: 1px solid %REDMID%; }
QPushButton[kind="danger"]:hover { background: %RED%; color: #ffffff; }
QPushButton[kind="chip"] { background: %SURF%; border: 1px solid %BORDER%; border-radius: 14px; padding: 5px 14px; font-weight: 500; }
QPushButton[kind="chip"]:checked { background: %ACCENTSOFT%; border-color: %ACCENT%; color: %ACCENT%; font-weight: 700; }
QPushButton[kind="tile"] { background: %SURF%; border: 1px solid %BORDER%; border-radius: %RL%px; padding: 0; }
QPushButton[kind="tile"]:checked { border: 2px solid %ACCENT%; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: %RS%px; padding: 6px; }
QToolButton:hover { background: %HOVER%; border-color: %BORDER%; }
QToolButton::menu-indicator { image: none; }

/* ── inputs ── */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit, QTextBrowser, QDateEdit {
  background: %SURF%; border: 1px solid %BORDER%; border-radius: %RS%px; padding: 6px 9px; color: %TEXT%;
  selection-background-color: %SELECT%; selection-color: %TEXT%; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QPlainTextEdit:hover { border-color: %ACCENTMID%; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: %ACCENT%; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: %SURF%; border: 1px solid %BORDER%; selection-background-color: %SELECT%; outline: 0; padding: 4px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 16px; border: none; background: transparent; }
QCheckBox, QRadioButton { background: transparent; spacing: 9px; }
QCheckBox::indicator, QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid %BORDER%; background: %SURF%; }
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %ACCENT%; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; }
QSlider::groove:horizontal { height: 4px; background: %BORDER%; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 2px; }
QSlider::handle:horizontal { width: 16px; height: 16px; margin: -6px 0; border-radius: 8px; background: %TEXT%; border: 2px solid %ACCENT%; }

/* ── tables & lists ── */
QTableView, QTreeView, QListView { background: %SURF%; alternate-background-color: %ALT%; border: 1px solid %BORDER%;
  border-radius: %RL%px; gridline-color: transparent; selection-background-color: %SELECT%; selection-color: %TEXT%; outline: 0; }
QTableView::item, QTreeView::item { padding: 6px 8px; border: none; border-bottom: 1px solid %ROWLINE%; }
QTableView::item:hover, QTreeView::item:hover { background: %HOVER%; }
QTableView::item:selected, QTreeView::item:selected { background: %SELECT%; }
QListView::item { padding: 7px 9px; border-radius: %RS%px; }
QListView::item:hover { background: %HOVER%; }
QListView::item:selected { background: %SELECT%; color: %TEXT%; }
QHeaderView { background: transparent; border: none; }
QHeaderView::section { background: %SURF2%; color: %MUTED%; border: none; border-bottom: 1px solid %BORDER%;
  padding: 9px 8px; font-family: "%HEAD%"; font-size: %F8%; font-weight: 700; }
QHeaderView::section:first { border-top-left-radius: %RL%px; }
QHeaderView::section:last { border-top-right-radius: %RL%px; }
QTableCornerButton::section { background: %SURF2%; border: none; }

/* ── scrollbars, progress, splitters ── */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: %BORDER%; border-radius: 4px; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: %ACCENTMID%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QProgressBar { background: %SURF2%; border: none; border-radius: 3px; max-height: 6px; text-align: center; color: transparent; }
QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %ACCENT%, stop:1 %ACCENTHOVER%); border-radius: 3px; }
QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: %ACCENTSOFT%; }

/* ── groups, menus, tabs, dialogs ── */
QGroupBox { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %SURF2%, stop:1 %SURF%); border: 1px solid %BORDER%;
  border-radius: %RL%px; margin-top: 18px; padding: 18px 16px 14px 16px; }
QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: %ACCENT%; background: transparent;
  font-family: "%HEAD%"; font-size: %F9%; font-weight: 700; }
QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QRadioButton { background: transparent; }
QMenu { background: %SURF%; border: 1px solid %BORDER%; border-radius: %RL%px; padding: 6px; }
QMenu::item { padding: 7px 24px 7px 12px; border-radius: %RS%px; }
QMenu::item:selected { background: %SELECT%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 5px 8px; }
QTabWidget::pane { border: none; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: %MUTED%; padding: 9px 18px; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:hover { color: %TEXT%; }
QTabBar::tab:selected { color: %TEXT%; border-bottom-color: %ACCENT%; }
QWizard, QDialog { background: %BG%; }
QDialog#Palette { background: %SURF%; border: 1px solid %ACCENT%; border-radius: %RL%px; }
QDialog#Palette QLineEdit { font-size: %F12%; border: none; border-bottom: 1px solid %BORDER%; border-radius: 0; padding: 12px 14px; background: transparent; }
QDialog#Palette QListWidget { border: none; background: transparent; }
QFrame#Toast { background: %SURF2%; border: 1px solid %ACCENT%; border-radius: %RL%px; }
QFrame#Toast QLabel { background: transparent; }
QFrame#Banner { background: %ACCENTSOFT%; border: 1px solid %ACCENT%; border-radius: %RL%px; }
QFrame#Banner QLabel { background: transparent; }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QFrame#Hero QLabel, QFrame#Hero QWidget#HeroRight { background: transparent; }
)");
    if (p.uppercase && !p.mono)  // game-menu look: condensed display face on buttons, tabs and the side bar
        s += QStringLiteral("QPushButton, QListWidget#Nav, QTabBar::tab, QHeaderView::section { font-family: \"%HEAD%\"; }\n"
                            "QPushButton#Omni { font-family: \"%FONT%\"; }\n");
    const QColor accentHover = p.accent.lighter(p.accent.lightness() > 160 ? 108 : 118);
    const QColor alt = p.surface.lightness() > 128 ? p.surface.darker(102) : p.surface.lighter(106);
    s.replace("%FONT%", p.mono ? theme::monoFont() : theme::uiFont())
        .replace("%HEAD%", p.mono ? theme::monoFont() : theme::headingFont())
        .replace("%MONO%", theme::monoFont())
        .replace("%F10%", pt(10)).replace("%F15%", pt(15)).replace("%F9%", pt(9)).replace("%F8%", pt(8))
        .replace("%F24%", pt(24)).replace("%F20%", pt(20)).replace("%F11%", pt(11)).replace("%F12%", pt(12))
        .replace("%RS%", QString::number(rs)).replace("%RL%", QString::number(rl)).replace("%R%", QString::number(r))
        .replace("%BG%", c(p.bg)).replace("%SURF2%", c(p.surface2)).replace("%SURF%", c(p.surface))
        .replace("%BORDER%", c(p.border)).replace("%TEXT%", c(p.text)).replace("%MUTED%", c(p.muted))
        .replace("%ACCENTTEXT%", c(p.accentText)).replace("%ACCENTHOVER%", c(accentHover))
        .replace("%ACCENTSOFT%", rgba(p.accent, 38)).replace("%ACCENTMID%", rgba(p.accent, 130)).replace("%ACCENT%", c(p.accent))
        .replace("%HOVER%", c(p.hover)).replace("%SELECT%", c(p.select)).replace("%NAV%", c(p.nav)).replace("%APPBAR%", c(p.appbar))
        .replace("%GREENSOFT%", rgba(p.green, 30)).replace("%GREEN%", c(p.green))
        .replace("%REDSOFT%", rgba(p.red, 30)).replace("%REDMID%", rgba(p.red, 140)).replace("%RED%", c(p.red))
        .replace("%ALT%", c(alt)).replace("%ROWLINE%", rgba(p.border, 120));
    return s;
}

QString pick(std::initializer_list<const char*> wanted, const QString& fallback) {
    const QStringList fams = QFontDatabase::families();
    for (const char* w : wanted) if (fams.contains(w)) return QString(w);
    return fallback;
}
}  // namespace

namespace theme {

QString uiFont() {
    static const QString f = pick({"Segoe UI Variable Text", "Segoe UI", "SF Pro Text", "Inter", "Roboto", "Cantarell", "Noto Sans",
                                   "DejaVu Sans"}, QApplication::font().family());
    return f;
}
QString monoFont() {
    static const QString f = pick({"Cascadia Mono", "Consolas", "JetBrains Mono", "SF Mono", "Menlo", "DejaVu Sans Mono"},
                                  QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    return f;
}
QString headingFont() {  // Bahnschrift ships with Windows 10/11: a DIN-style face that suits a military look
    static const QString f = pick({"Bahnschrift SemiBold", "Bahnschrift", "Rajdhani", "Oswald", "Barlow Condensed", "Roboto Condensed",
                                   "Segoe UI Variable Display", "Segoe UI Semibold", "DejaVu Sans Condensed"}, uiFont());
    return f;
}

void init(const QString& dir) {
    g_userDir = dir;
    if (!dir.isEmpty()) QDir().mkpath(dir);
    loadUser();
}

bool isBuiltin(const QString& name) {
    for (const auto& p : builtins()) if (p.name == name) return true;
    return false;
}

Palette get(const QString& nameIn) {
    QString name = nameIn == "dark" ? QStringLiteral("warzone") : nameIn == "light" ? QStringLiteral("arctic") : nameIn;
    for (const auto& p : builtins()) if (p.name == name) return p;
    for (const auto& p : g_user) if (p.name == name) return p;
    return builtins().front();
}

const Palette& current() { return g_cur; }

QStringList names() {
    QStringList out;
    for (const auto& p : builtins()) out << p.name;
    for (const auto& p : g_user) out << p.name;
    return out;
}

void applyPalette(const Palette& p) {
    g_cur = p;
    art::clearCache();
    qApp->setStyle(QStyleFactory::create("Fusion"));  // same look on every OS; QSS does the rest
    QPalette pal;
    pal.setColor(QPalette::Window, p.bg);
    pal.setColor(QPalette::WindowText, p.text);
    pal.setColor(QPalette::Base, p.surface);
    pal.setColor(QPalette::AlternateBase, p.surface2);
    pal.setColor(QPalette::Text, p.text);
    pal.setColor(QPalette::Button, p.surface2);
    pal.setColor(QPalette::ButtonText, p.text);
    pal.setColor(QPalette::Highlight, p.select);
    pal.setColor(QPalette::HighlightedText, p.text);
    pal.setColor(QPalette::ToolTipBase, p.surface2);
    pal.setColor(QPalette::ToolTipText, p.text);
    pal.setColor(QPalette::PlaceholderText, p.muted);
    pal.setColor(QPalette::Link, p.accent);
    pal.setColor(QPalette::LinkVisited, p.accent.darker(115));
    pal.setColor(QPalette::Disabled, QPalette::Text, p.muted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, p.muted);
    qApp->setPalette(pal);
    qApp->setStyleSheet(css(p));
}

void apply(const QString& name) { applyPalette(get(name)); }

QColor statusColor(const QString& s) {
    const Palette& p = current();
    if (s == "READY") return p.green;
    if (s == "SUCCESS" || s == "ALREADY_REDEEMED" || s == "SUBMITTED") return p.blue;
    if (s == "LIKELY_EXPIRED" || s == "LIKELY_EXHAUSTED" || s == "UNKNOWN" || s == "REGION_MISMATCH") return p.yellow;
    if (s == "INVALID" || s == "REJECTED") return p.red;
    if (s == "EXPIRED" || s == "LIMIT_REACHED" || s == "IGNORED") return p.grey;
    return p.muted;
}

QColor verdictColor(const QString& v) {
    const Palette& p = current();
    if (v == "LIKELY WORKS") return p.green;
    if (v == "UNCERTAIN") return p.yellow;
    if (v.contains("BY YOU")) return p.blue;
    if (v.startsWith("DEAD")) return p.red;
    if (v.contains("DEAD")) return p.grey;
    return p.muted;
}

// ── your own themes ─────────────────────────────────────────────
QStringList colorKeys() {
    return {"accent", "accentText", "bg", "surface", "surface2", "border", "text", "muted", "hover", "select", "nav", "appbar",
            "green", "yellow", "red", "blue", "grey", "purple"};
}

QColor* colorRef(Palette& p, const QString& k) {
    if (k == "bg") return &p.bg;
    if (k == "surface") return &p.surface;
    if (k == "surface2") return &p.surface2;
    if (k == "border") return &p.border;
    if (k == "text") return &p.text;
    if (k == "muted") return &p.muted;
    if (k == "accent") return &p.accent;
    if (k == "accentText") return &p.accentText;
    if (k == "hover") return &p.hover;
    if (k == "select") return &p.select;
    if (k == "nav") return &p.nav;
    if (k == "appbar") return &p.appbar;
    if (k == "green") return &p.green;
    if (k == "yellow") return &p.yellow;
    if (k == "red") return &p.red;
    if (k == "blue") return &p.blue;
    if (k == "grey") return &p.grey;
    if (k == "purple") return &p.purple;
    return nullptr;
}

QString colorLabel(const QString& k) {
    static const QHash<QString, QString> m = {
        {"accent", "Accent"}, {"accentText", "Text on accent"}, {"bg", "Background"}, {"surface", "Panels"},
        {"surface2", "Raised panels"}, {"border", "Borders"}, {"text", "Text"}, {"muted", "Secondary text"},
        {"hover", "Hover"}, {"select", "Selection"}, {"nav", "Side bar"}, {"appbar", "Top bar"}, {"green", "Ready / good"},
        {"yellow", "Unsure / warning"}, {"red", "Dead / error"}, {"blue", "Redeemed / info"}, {"grey", "Expired"},
        {"purple", "Extra"}};
    return m.value(k, k);
}

QJsonObject toJson(const Palette& p) {
    QJsonObject colors;
    Palette copy = p;
    for (const auto& k : colorKeys()) colors[k] = colorRef(copy, k)->name(QColor::HexRgb);
    QJsonObject o;
    o["kind"] = "cod-redemption-hunter-theme";
    o["version"] = 1;
    o["label"] = p.label;
    o["colors"] = colors;
    o["pattern"] = p.pattern;
    o["intensity"] = p.intensity;
    o["radius"] = p.radius;
    o["uppercase_titles"] = p.uppercase;
    o["font_scale"] = p.fontScale;
    o["weapon"] = p.weapon;
    o["mono"] = p.mono;
    return o;
}

bool fromJson(const QJsonObject& o, Palette* out, QString* error) {
    auto fail = [error](const QString& e) { if (error) *error = e; return false; };
    if (o.isEmpty()) return fail("That isn't a theme file.");
    const QJsonObject colors = o.value("colors").toObject();
    if (colors.isEmpty()) return fail("The theme has no colours.");
    Palette p = builtins().front();
    for (const auto& k : colorKeys()) {
        if (!colors.contains(k)) continue;
        QColor col(colors.value(k).toString());
        if (!col.isValid()) return fail("Bad colour for " + k + ".");
        *colorRef(p, k) = col;
    }
    p.label = o.value("label").toString().left(40).trimmed();
    if (p.label.isEmpty()) p.label = "My theme";
    const QString pat = o.value("pattern").toString("digital");
    p.pattern = art::patternKinds().contains(pat) ? pat : QStringLiteral("digital");
    p.intensity = std::clamp(o.value("intensity").toInt(50), 0, 100);
    p.radius = std::clamp(o.value("radius").toInt(8), 0, 16);
    p.uppercase = o.value("uppercase_titles").toBool(true);
    p.fontScale = std::clamp(o.value("font_scale").toInt(100), 85, 130);
    const QString w = o.value("weapon").toString("ar");
    p.weapon = w == "none" || !art::weapon(w).isEmpty() ? w : QStringLiteral("ar");
    p.mono = o.value("mono").toBool(false);
    p.builtin = false;
    *out = p;
    return true;
}

QString saveUser(Palette p, QString* error) {
    if (g_userDir.isEmpty()) { if (error) *error = "No folder for themes."; return {}; }
    QString name = p.builtin || p.name.isEmpty() || isBuiltin(p.name) ? slug(p.label) : p.name;
    if (isBuiltin(name)) name += "-custom";
    QFile f(QDir(g_userDir).filePath(name + ".json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (error) *error = f.errorString(); return {}; }
    f.write(QJsonDocument(toJson(p)).toJson(QJsonDocument::Indented));
    f.close();
    loadUser();
    return name;
}

bool removeUser(const QString& name) {
    if (isBuiltin(name) || g_userDir.isEmpty()) return false;
    bool ok = QFile::remove(QDir(g_userDir).filePath(name + ".json"));
    loadUser();
    return ok;
}

QString importFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return {}; }
    if (f.size() > 64 * 1024) { if (error) *error = "That file is too big for a theme."; return {}; }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) { if (error) *error = pe.errorString(); return {}; }
    Palette p;
    if (!fromJson(doc.object(), &p, error)) return {};
    p.name.clear();
    return saveUser(p, error);
}

bool exportFile(const QString& name, const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (error) *error = f.errorString(); return false; }
    f.write(QJsonDocument(toJson(get(name))).toJson(QJsonDocument::Indented));
    return true;
}

}  // namespace theme
