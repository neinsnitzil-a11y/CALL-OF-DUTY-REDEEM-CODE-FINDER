// Stats & patches, For you (playstyle picks + your match log), Community builds (GitHub).
#pragma once
#include <QWidget>

#include "../core/json.hpp"
#include "pages.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextBrowser;

// Time-to-kill over distance for two weapons.
class TtkChart : public QWidget {
    Q_OBJECT
public:
    explicit TtkChart(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(220); }
    void setCurves(const QString& a, const std::vector<std::pair<int, int>>& ca, const QString& b,
                   const std::vector<std::pair<int, int>>& cb);

protected:
    void paintEvent(class QPaintEvent*) override;

private:
    QString a_, b_;
    std::vector<std::pair<int, int>> ca_, cb_;
};

class StatsPage : public Page {
    Q_OBJECT
public:
    explicit StatsPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

private:
    void compare();
    QString game() const;
    QComboBox *game_, *wa_, *wb_;
    QSpinBox* health_;
    QCheckBox* useAi_;
    QTableWidget* table_;
    TtkChart* chart_;
    QListWidget *patches_, *changes_;
    QPlainTextEdit* log_;
    QPushButton *research_, *checkPatches_;
    hunter::Json rows_;
};

class ForYouPage : public Page {
    Q_OBJECT
public:
    explicit ForYouPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

private:
    void saveProfile();
    void logMatch();
    QString game() const;
    QComboBox *game_, *input_, *style_, *mode_;
    QListWidget* recs_;
    QTextBrowser* detail_;
    QTableWidget* perf_;
    hunter::Json items_;
};

class CommunityPage : public Page {
    Q_OBJECT
public:
    explicit CommunityPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

private:
    void showBuild();
    hunter::Json selected() const;
    void setup();
    void submit();
    void toggleFollow(const QString& login);
    QLabel* status_;
    QComboBox *game_, *sort_;
    QTableWidget *builds_, *creators_;
    QTextBrowser* detail_;
    hunter::Json list_, creatorList_;
    // submit form
    QComboBox *sGame_, *sMode_, *sStyle_, *sRange_, *sInput_;
    QLineEdit *sWeapon_, *sClass_, *sPerks_, *sEquip_, *sSeason_;
    QPlainTextEdit *sAttach_, *sNote_;
};
