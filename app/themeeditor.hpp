// Theme gallery tiles and the theme editor (colours, camo pattern, corners, text size, banner weapon).
#pragma once
#include <QDialog>
#include <QMap>
#include <QPixmap>

#include "theme.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSlider;

QPixmap themePreview(const Palette& p, const QSize& size, qreal dpr = 1.0);

class ThemeEditor : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditor(const Palette& start, QWidget* parent = nullptr);
    QString savedName() const { return saved_; }

protected:
    void reject() override;

private:
    void changed();
    void pickColor(const QString& key);
    void refreshSwatches();
    Palette p_, original_;
    QString saved_;
    QLineEdit* name_;
    QComboBox *pattern_, *weapon_;
    QSlider *intensity_, *radius_, *font_;
    QCheckBox *upper_, *mono_;
    QMap<QString, QPushButton*> swatches_;
};
