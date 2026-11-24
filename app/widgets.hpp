// Building blocks for the tactical look: page banners, stat cards, toasts, icon buttons.
#pragma once
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QWidget>

class QVBoxLayout;
class QPropertyAnimation;

// kind: accent | ghost | danger | chip | tile. A leading emoji ("📊 Research") becomes a drawn icon.
QPushButton* button(const QString& text, const QString& kind = "", QWidget* parent = nullptr);
QPushButton* iconButton(const QString& icon, const QString& text, const QString& kind = "", QWidget* parent = nullptr);
QLabel* label(const QString& text, const QString& objectName = "", QWidget* parent = nullptr);
QWidget* pageHeader(const QString& title, const QString& hint, QWidget* right = nullptr);  // a HeroBanner
void repolish(QWidget* w);  // re-apply the style sheet after a dynamic property changed
void retintIcons(QWidget* root);  // after a theme change: redraw icons in the new colours

// A frame with the theme's camo / contour pattern faded across it (side bar, top bar).
class TacticalFrame : public QFrame {
public:
    enum Fade { FadeDown, FadeRight, FadeUp };
    TacticalFrame(const QString& objectName, Fade fade, int strength = 40, QWidget* parent = nullptr);

protected:
    void paintEvent(class QPaintEvent* e) override;

private:
    Fade fade_;
    int strength_;
};

// Page banner: camo / contour background, a weapon-class outline, corner brackets, title + hint.
class HeroBanner : public QFrame {
    Q_OBJECT
public:
    HeroBanner(const QString& title, const QString& hint, QWidget* right = nullptr, const QString& art = QString(),
               QWidget* parent = nullptr);
    QLabel* titleLabel() const { return title_; }
    QLabel* hintLabel() const { return hint_; }
    void setArt(const QString& weapon) { art_ = weapon; update(); }

protected:
    void paintEvent(class QPaintEvent*) override;
    void changeEvent(QEvent* e) override;

private:
    void restyle();
    QLabel *kicker_, *title_, *hint_;
    QString art_;
    quint32 seed_;
};

class StatCard : public QFrame {
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setShown)
public:
    StatCard(const QString& title, const QColor& accent, const QString& icon = QString(), QWidget* parent = nullptr);
    void setValue(int v);  // animates the number
    int value() const { return shown_; }
    void setShown(int v);
    void setAccent(const QColor& c);

signals:
    void clicked();

protected:
    void mousePressEvent(class QMouseEvent* e) override;
    void paintEvent(class QPaintEvent* e) override;
    void enterEvent(class QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QLabel *num_, *lbl_;
    QColor accent_;
    QString icon_;
    bool hover_ = false;
    int shown_ = 0, target_ = 0;
    QPropertyAnimation* anim_;
};

// A message that slides in at the bottom-right of the window and fades away.
class Toast : public QFrame {
    Q_OBJECT
public:
    static void popup(QWidget* window, const QString& text, bool ok = true, int ms = 3500);

private:
    Toast(QWidget* window, const QString& text, bool ok);
};
