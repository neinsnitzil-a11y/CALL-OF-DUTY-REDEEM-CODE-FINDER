#include "themeeditor.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "art.hpp"
#include "widgets.hpp"

QPixmap themePreview(const Palette& p, const QSize& size, qreal dpr) {
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    const QRectF r(0, 0, size.width(), size.height());
    QPainterPath clip;
    clip.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), std::max(2, p.radius), std::max(2, p.radius));
    g.setClipPath(clip);
    g.fillRect(r, p.bg);
    if (p.pattern != "none") g.drawPixmap(0, 0, art::pattern(p.pattern, size, p, p.intensity, 3, dpr));
    // a tiny window: side bar, top bar, a card
    g.fillRect(QRectF(0, 0, r.width() * 0.22, r.height()), p.nav);
    g.fillRect(QRectF(0, 0, r.width(), 14), p.appbar);
    g.fillRect(QRectF(6, 24, r.width() * 0.22 - 12, 6), p.accent);
    for (int i = 0; i < 3; ++i) g.fillRect(QRectF(6, 36 + i * 10, r.width() * 0.22 - 16, 4), p.muted);
    QRectF card(r.width() * 0.27, 22, r.width() * 0.68, r.height() * 0.42);
    g.setPen(QPen(p.border, 1));
    g.setBrush(p.surface);
    g.drawRoundedRect(card, std::max(1, p.radius / 2), std::max(1, p.radius / 2));
    g.fillRect(QRectF(card.left(), card.top(), card.width(), 2), p.accent);
    QColor wf = p.accent;
    wf.setAlpha(60);
    if (p.weapon != "none") art::drawWeapon(g, p.weapon, card.adjusted(card.width() * 0.4, 4, -4, -4), wf);
    for (int i = 0; i < 3; ++i) {
        QColor c = i == 0 ? p.green : i == 1 ? p.yellow : p.red;
        g.fillRect(QRectF(card.left() + 8 + i * 14, card.bottom() - 10, 9, 5), c);
    }
    g.setClipping(false);
    QFont f(p.mono ? theme::monoFont() : theme::headingFont());
    f.setPointSizeF(9);
    f.setBold(true);
    if (p.uppercase) f.setCapitalization(QFont::AllUppercase);
    g.setFont(f);
    g.setPen(p.text);
    g.drawText(QRectF(r.width() * 0.27, r.height() - 24, r.width() * 0.7, 20), Qt::AlignLeft | Qt::AlignVCenter, p.label);
    g.setPen(QPen(p.border, 1));
    g.setBrush(Qt::NoBrush);
    g.drawPath(clip);
    return pm;
}

ThemeEditor::ThemeEditor(const Palette& start, QWidget* parent) : QDialog(parent), p_(start), original_(theme::current()) {
    setWindowTitle("Theme editor");
    resize(760, 620);
    if (p_.builtin) {
        p_.label = p_.label + " (custom)";
        p_.name.clear();
    }
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(20, 18, 20, 16);
    v->addWidget(label("Changes show live across the app. Save keeps them as your own theme; Cancel puts everything back.",
                       "PageHint"));
    auto* cols = new QHBoxLayout;
    auto* look = new QGroupBox("Look");
    auto* f = new QFormLayout(look);
    name_ = new QLineEdit(p_.label);
    pattern_ = new QComboBox;
    const std::pair<const char*, const char*> pats[] = {{"digital", "Digital camo"}, {"woodland", "Woodland camo"},
                                                        {"topo", "Topographic map"}, {"grid", "HUD grid"}, {"none", "None"}};
    for (auto& [k, l] : pats) pattern_->addItem(l, k);
    pattern_->setCurrentIndex(std::max(0, pattern_->findData(p_.pattern)));
    weapon_ = new QComboBox;
    const std::pair<const char*, const char*> guns[] = {{"ar", "Assault rifle"}, {"smg", "SMG"}, {"lmg", "LMG"}, {"sniper", "Sniper"},
                                                        {"marksman", "Marksman"}, {"shotgun", "Shotgun"}, {"pistol", "Pistol"},
                                                        {"launcher", "Launcher"}, {"knife", "Knife"}, {"none", "None"}};
    for (auto& [k, l] : guns) weapon_->addItem(l, k);
    weapon_->setCurrentIndex(std::max(0, weapon_->findData(p_.weapon)));
    auto slider = [](int lo, int hi, int val) {
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        s->setValue(val);
        return s;
    };
    intensity_ = slider(0, 100, p_.intensity);
    radius_ = slider(0, 16, p_.radius);
    font_ = slider(85, 130, p_.fontScale);
    upper_ = new QCheckBox("UPPERCASE page titles");
    upper_->setChecked(p_.uppercase);
    mono_ = new QCheckBox("Terminal font everywhere");
    mono_->setChecked(p_.mono);
    f->addRow("Name", name_);
    f->addRow("Background", pattern_);
    f->addRow("Pattern strength", intensity_);
    f->addRow("Banner weapon", weapon_);
    f->addRow("Corner rounding", radius_);
    f->addRow("Text size", font_);
    f->addRow(upper_);
    f->addRow(mono_);
    cols->addWidget(look, 1);

    auto* colors = new QGroupBox("Colours");
    auto* grid = new QGridLayout(colors);
    int i = 0;
    for (const auto& k : theme::colorKeys()) {
        auto* b = new QPushButton;
        b->setFixedSize(34, 24);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, k] { pickColor(k); });
        swatches_[k] = b;
        grid->addWidget(b, i / 2, (i % 2) * 2);
        grid->addWidget(new QLabel(theme::colorLabel(k)), i / 2, (i % 2) * 2 + 1);
        ++i;
    }
    cols->addWidget(colors, 1);
    v->addLayout(cols, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &ThemeEditor::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        changed();
        QString err;
        saved_ = theme::saveUser(p_, &err);
        if (saved_.isEmpty()) { QMessageBox::warning(this, "Theme", err); return; }
        accept();
    });
    connect(name_, &QLineEdit::textChanged, this, [this] { p_.label = name_->text().trimmed().left(40); });
    for (auto* c : {pattern_, weapon_}) connect(c, &QComboBox::currentIndexChanged, this, &ThemeEditor::changed);
    for (auto* sl : {intensity_, radius_, font_}) {
        connect(sl, &QSlider::sliderReleased, this, &ThemeEditor::changed);
        connect(sl, &QSlider::valueChanged, this, [this, sl] { if (!sl->isSliderDown()) changed(); });
    }
    for (auto* c : {upper_, mono_}) connect(c, &QCheckBox::toggled, this, &ThemeEditor::changed);
    refreshSwatches();
    theme::applyPalette(p_);
}

void ThemeEditor::refreshSwatches() {
    for (auto it = swatches_.begin(); it != swatches_.end(); ++it) {
        const QColor c = *theme::colorRef(p_, it.key());
        it.value()->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:1px solid %2;border-radius:4px;}")
                                      .arg(c.name(), p_.border.name()));
        it.value()->setToolTip(c.name());
    }
}

void ThemeEditor::changed() {
    p_.label = name_->text().trimmed().isEmpty() ? QStringLiteral("My theme") : name_->text().trimmed().left(40);
    p_.pattern = pattern_->currentData().toString();
    p_.weapon = weapon_->currentData().toString();
    p_.intensity = intensity_->value();
    p_.radius = radius_->value();
    p_.fontScale = font_->value();
    p_.uppercase = upper_->isChecked();
    p_.mono = mono_->isChecked();
    theme::applyPalette(p_);
    refreshSwatches();
}

void ThemeEditor::pickColor(const QString& key) {
    QColor* ref = theme::colorRef(p_, key);
    QColor c = QColorDialog::getColor(*ref, this, theme::colorLabel(key));
    if (!c.isValid()) return;
    *ref = c;
    changed();
}

void ThemeEditor::reject() {
    theme::applyPalette(original_);
    QDialog::reject();
}
