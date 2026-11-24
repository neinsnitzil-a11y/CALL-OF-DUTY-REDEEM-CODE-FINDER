#include "widgets.hpp"

#include <QEnterEvent>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "art.hpp"
#include "theme.hpp"

namespace {
QColor iconColorFor(const QString& kind) {
    const Palette& p = theme::current();
    if (kind == "accent") return p.accentText;
    if (kind == "danger") return p.red;
    if (kind == "ghost") return p.muted;
    return p.text;
}
void setButtonIcon(QPushButton* b) {
    const QString name = b->property("iconName").toString();
    if (name.isEmpty()) return;
    b->setIcon(art::icon(name, iconColorFor(b->property("kind").toString())));
    b->setIconSize(QSize(16, 16));
}

// What the banner says above the title, and which outline it shows.
struct HeroArt {
    const char *key, *kicker, *weapon;
};
const HeroArt kHero[] = {
    {"All games", "INTEL // CODES", "ar"},           {"Sources", "RECON // SOURCES", "marksman"},
    {"Activity", "LOG // ACTIVITY", "pistol"},       {"Discord", "COMMS // DISCORD", "launcher"},
    {"Account", "SECURE // ACCOUNT", "shotgun"},     {"Settings", "SYSTEM // SETTINGS", "knife"},
    {"AI helper", "INTEL // AI", "smg"},             {"Builds", "ARMORY // LOADOUTS", "ar"},
    {"Screenshots", "ARMORY // GALLERY", "marksman"}, {"Discord studio", "COMMS // STUDIO", "launcher"},
    {"Weapon stats", "ARMORY // BALLISTICS", "sniper"}, {"For you", "ARMORY // FOR YOU", "smg"},
    {"Community", "SQUAD // COMMUNITY", "lmg"},
};
const HeroArt* heroFor(const QString& title) {
    for (const auto& h : kHero) if (title.startsWith(h.key)) return &h;
    return nullptr;
}
}  // namespace

void tacticalFont(QPushButton* b) {
    if (!theme::current().uppercase || b->property("kind").toString() == "tile") return;
    QFont f = b->font();
    f.setFamily(theme::current().mono ? theme::monoFont() : theme::headingFont());
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    b->setFont(f);
}

QPushButton* button(const QString& textIn, const QString& kind, QWidget* parent) {
    QString text;
    const QString ic = art::iconForPrefix(textIn, &text);
    auto* b = new QPushButton(ic.isEmpty() ? textIn : text, parent);
    if (!kind.isEmpty()) b->setProperty("kind", kind);
    b->setCursor(Qt::PointingHandCursor);
    if (kind == "chip") b->setCheckable(true);
    if (!ic.isEmpty()) {
        b->setProperty("iconName", ic);
        setButtonIcon(b);
        if (text.isEmpty()) b->setToolTip(textIn);
    }
    tacticalFont(b);
    return b;
}

QPushButton* iconButton(const QString& icon, const QString& text, const QString& kind, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    if (!kind.isEmpty()) b->setProperty("kind", kind);
    b->setCursor(Qt::PointingHandCursor);
    b->setProperty("iconName", icon);
    setButtonIcon(b);
    tacticalFont(b);
    return b;
}

void retintIcons(QWidget* root) {
    if (!root) return;
    if (auto* self = qobject_cast<QPushButton*>(root)) setButtonIcon(self);
    for (auto* b : root->findChildren<QPushButton*>()) setButtonIcon(b);
}

QLabel* label(const QString& text, const QString& objectName, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    if (!objectName.isEmpty()) l->setObjectName(objectName);
    l->setWordWrap(objectName == "PageHint" || objectName == "Muted");
    l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    l->setOpenExternalLinks(true);
    return l;
}

QWidget* pageHeader(const QString& title, const QString& hint, QWidget* right) { return new HeroBanner(title, hint, right); }

void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

// ── TacticalFrame ───────────────────────────────────────────────
TacticalFrame::TacticalFrame(const QString& name, Fade fade, int strength, QWidget* parent)
    : QFrame(parent), fade_(fade), strength_(strength) {
    setObjectName(name);
}

void TacticalFrame::paintEvent(QPaintEvent* e) {
    QFrame::paintEvent(e);
    const Palette& p = theme::current();
    if (p.pattern == "none" || width() < 8 || height() < 8) return;
    QPixmap pat = art::pattern(p.pattern, size(), p, p.intensity * strength_ / 100, 11 + quint32(width() > height()), devicePixelRatioF());
    if (pat.isNull()) return;
    QPixmap faded(pat.size());
    faded.setDevicePixelRatio(pat.devicePixelRatio());
    faded.fill(Qt::transparent);
    QPainter fp(&faded);
    fp.drawPixmap(0, 0, pat);
    fp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    QLinearGradient m = fade_ == FadeRight ? QLinearGradient(0, 0, width(), 0)
                        : fade_ == FadeUp  ? QLinearGradient(0, height(), 0, 0)
                                           : QLinearGradient(0, 0, 0, height());
    m.setColorAt(0, QColor(0, 0, 0, fade_ == FadeRight ? 0 : 200));
    m.setColorAt(fade_ == FadeRight ? 0.6 : 0.5, QColor(0, 0, 0, 90));
    m.setColorAt(1, QColor(0, 0, 0, fade_ == FadeRight ? 220 : 0));
    fp.fillRect(QRect(0, 0, width(), height()), m);
    fp.end();
    QPainter g(this);
    g.drawPixmap(0, 0, faded);
    if (fade_ == FadeRight) {  // accent hairline under the top bar
        QLinearGradient line(0, 0, width(), 0);
        QColor a = p.accent;
        line.setColorAt(0, a);
        a.setAlpha(0);
        line.setColorAt(0.45, a);
        g.fillRect(QRectF(0, height() - 2, width(), 2), line);
    }
}

// ── HeroBanner ──────────────────────────────────────────────────
HeroBanner::HeroBanner(const QString& title, const QString& hint, QWidget* right, const QString& art, QWidget* parent)
    : QFrame(parent), seed_(static_cast<quint32>(qHash(title))) {
    setObjectName("Hero");
    setMinimumHeight(112);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    const HeroArt* h = heroFor(title);
    art_ = !art.isEmpty() ? art : h ? QString(h->weapon) : QStringLiteral("ar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(26, 18, 22, 18);
    lay->setSpacing(16);
    auto* text = new QVBoxLayout;
    text->setSpacing(3);
    kicker_ = new QLabel(h ? h->kicker : "COD // HUNTER");
    kicker_->setObjectName("Kicker");
    kicker_->setProperty("noTranslate", true);
    title_ = label(title, "PageTitle");
    title_->setTextInteractionFlags(Qt::NoTextInteraction);
    hint_ = label(hint, "PageHint");
    hint_->setMaximumWidth(760);
    hint_->setVisible(!hint.isEmpty());
    text->addWidget(kicker_);
    text->addWidget(title_);
    text->addWidget(hint_);
    lay->addLayout(text, 1);
    if (right) {
        if (right->objectName().isEmpty() && QByteArray(right->metaObject()->className()) == "QWidget")
            right->setObjectName("HeroRight");  // plain containers see through to the banner
        lay->addWidget(right, 0, Qt::AlignBottom | Qt::AlignRight);
    }
    restyle();
}

void HeroBanner::restyle() {
    const Palette& p = theme::current();
    QFont f = title_->font();
    f.setCapitalization(p.uppercase ? QFont::AllUppercase : QFont::MixedCase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, p.uppercase ? 1.2 : 0);
    title_->setFont(f);
    QFont k = kicker_->font();
    k.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    kicker_->setFont(k);
}

void HeroBanner::changeEvent(QEvent* e) {
    if (e->type() == QEvent::StyleChange || e->type() == QEvent::PaletteChange) restyle();
    QFrame::changeEvent(e);
}

void HeroBanner::paintEvent(QPaintEvent*) {
    const Palette& p = theme::current();
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const qreal rad = std::max(2, p.radius + 2);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(r, rad, rad);
    g.setClipPath(clip);

    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0, p.surface2);
    bg.setColorAt(1, p.surface);
    g.fillPath(clip, bg);

    // background pattern, fading in from the left
    if (p.pattern != "none") {
        QPixmap pat = art::pattern(p.pattern, size(), p, p.intensity, seed_, devicePixelRatioF());
        if (!pat.isNull()) {
            QPixmap faded(pat.size());
            faded.setDevicePixelRatio(pat.devicePixelRatio());
            faded.fill(Qt::transparent);
            QPainter fp(&faded);
            fp.drawPixmap(0, 0, pat);
            fp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            QLinearGradient mask(0, 0, width(), 0);
            mask.setColorAt(0.0, QColor(0, 0, 0, 0));
            mask.setColorAt(0.35, QColor(0, 0, 0, 60));
            mask.setColorAt(1.0, QColor(0, 0, 0, 255));
            fp.fillRect(QRect(0, 0, width(), height()), mask);
            fp.end();
            g.drawPixmap(0, 0, faded);
        }
    }
    // weapon outline on the right
    if (p.weapon != "none" && width() > 640) {
        QColor fill = p.accent;
        fill.setAlpha(26);
        QColor edge = p.accent;
        edge.setAlpha(90);
        const qreal h = height() * 0.95, w = h * 200.0 / 60.0;
        QRectF box(width() - w - 30, (height() - h) / 2 + 6, w, h);
        art::drawWeapon(g, art_.isEmpty() ? p.weapon : art_, box, fill, edge);
    }
    // accent tab at the bottom left + hairline border
    g.setClipping(false);
    g.setPen(QPen(p.border, 1));
    g.setBrush(Qt::NoBrush);
    g.drawRoundedRect(r, rad, rad);
    g.fillRect(QRectF(26, height() - 3, 72, 3), p.accent);
    QColor cc = p.accent;
    cc.setAlpha(170);
    art::drawCorners(g, r.adjusted(7, 7, -7, -7), cc, 9, 1.6);
}

// ── StatCard ────────────────────────────────────────────────────
StatCard::StatCard(const QString& title, const QColor& accent, const QString& icon, QWidget* parent)
    : QFrame(parent), accent_(accent), icon_(icon) {
    setObjectName("Card");
    setCursor(Qt::PointingHandCursor);
    setMinimumWidth(150);
    setMinimumHeight(84);
    setAttribute(Qt::WA_Hover);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(icon.isEmpty() ? 18 : 62, 10, 16, 10);
    auto* v = new QVBoxLayout;
    v->setSpacing(0);
    num_ = label("0", "CardNum");
    num_->setTextInteractionFlags(Qt::NoTextInteraction);
    lbl_ = label(title, "CardLbl");
    lbl_->setTextInteractionFlags(Qt::NoTextInteraction);
    QFont f = lbl_->font();
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    lbl_->setFont(f);
    v->addWidget(num_);
    v->addWidget(lbl_);
    h->addLayout(v, 1);
    anim_ = new QPropertyAnimation(this, "value", this);
    anim_->setDuration(450);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
}

void StatCard::setAccent(const QColor& c) {
    accent_ = c;
    update();
}

void StatCard::paintEvent(QPaintEvent* e) {
    QFrame::paintEvent(e);  // QSS background + border
    const Palette& p = theme::current();
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const qreal rad = std::max(2, p.radius + 2);
    QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath clip;
    clip.addRoundedRect(r, rad, rad);
    g.setClipPath(clip);
    // glow in the accent colour from the top
    QLinearGradient glow(0, 0, 0, height());
    QColor a = accent_;
    a.setAlpha(hover_ ? 46 : 26);
    glow.setColorAt(0, a);
    glow.setColorAt(0.7, QColor(0, 0, 0, 0));
    g.fillRect(rect(), glow);
    g.fillRect(QRectF(0, 0, width(), 3), accent_);
    if (!icon_.isEmpty()) {
        QRectF disc(16, (height() - 36) / 2.0, 36, 36);
        QColor bgc = accent_;
        bgc.setAlpha(36);
        g.setPen(Qt::NoPen);
        g.setBrush(bgc);
        g.drawRoundedRect(disc, std::max(2, p.radius), std::max(2, p.radius));
        art::drawIcon(g, icon_, disc.adjusted(8, 8, -8, -8), accent_, 2.0);
    }
    if (hover_) {
        QColor cc = accent_;
        cc.setAlpha(200);
        g.setClipping(false);
        art::drawCorners(g, r.adjusted(4, 4, -4, -4), cc, 7, 1.5);
    }
}

void StatCard::enterEvent(QEnterEvent* e) { hover_ = true; update(); QFrame::enterEvent(e); }
void StatCard::leaveEvent(QEvent* e) { hover_ = false; update(); QFrame::leaveEvent(e); }

void StatCard::setValue(int v) {
    if (v == target_) return;
    target_ = v;
    anim_->stop();
    anim_->setStartValue(shown_);
    anim_->setEndValue(v);
    anim_->start();
}

void StatCard::setShown(int v) {
    shown_ = v;
    num_->setText(QLocale().toString(v));
}

void StatCard::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit clicked();
    QFrame::mousePressEvent(e);
}

// ── Toast ───────────────────────────────────────────────────────
Toast::Toast(QWidget* window, const QString& text, bool ok) : QFrame(window) {
    setObjectName("Toast");
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(12, 10, 16, 10);
    h->setSpacing(10);
    const Palette& p = theme::current();
    auto* dot = new QLabel;
    dot->setPixmap(art::iconPixmap(ok ? "check" : "warning", ok ? p.green : p.red, 18, devicePixelRatioF()));
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setMaximumWidth(420);
    h->addWidget(dot, 0, Qt::AlignTop);
    h->addWidget(l, 1);
    setStyleSheet(QStringLiteral("QFrame#Toast{border-left:3px solid %1;}").arg((ok ? p.accent : p.red).name()));
    adjustSize();
}

void Toast::popup(QWidget* window, const QString& text, bool ok, int ms) {
    if (!window) return;
    // stack above toasts that are still showing
    int offset = 0;
    for (auto* t : window->findChildren<Toast*>(QString(), Qt::FindDirectChildrenOnly)) offset += t->height() + 8;
    auto* t = new Toast(window, text, ok);
    const int margin = 20;
    QPoint end(window->width() - t->width() - margin, window->height() - t->height() - margin - 30 - offset);
    t->move(end + QPoint(0, 20));
    auto* fx = new QGraphicsOpacityEffect(t);
    t->setGraphicsEffect(fx);
    fx->setOpacity(0);
    t->QFrame::show();
    t->raise();
    auto* slide = new QPropertyAnimation(t, "pos", t);
    slide->setDuration(220);
    slide->setEndValue(end);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    auto* fadeIn = new QPropertyAnimation(fx, "opacity", t);
    fadeIn->setDuration(220);
    fadeIn->setEndValue(1.0);
    slide->start();
    fadeIn->start();
    QTimer::singleShot(ms, t, [t, fx] {
        auto* fadeOut = new QPropertyAnimation(fx, "opacity", t);
        fadeOut->setDuration(400);
        fadeOut->setEndValue(0.0);
        QObject::connect(fadeOut, &QPropertyAnimation::finished, t, &QWidget::close);
        fadeOut->start();
    });
}
