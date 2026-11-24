// Content pages: AI helper (live chat), Builds & settings (curated + researched), Screenshot library,
// Discord studio.
#pragma once
#include <QString>
#include <QTimer>

#include "../core/content.hpp"
#include "../core/research.hpp"
#include "pages.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTextBrowser;
class QGroupBox;

QComboBox* gameCombo(bool currentOnly = false);  // CODM, MW4, BO7, Warzone, BO6 (+ older)

class StudioPage;

class AiPage : public Page {
    Q_OBJECT
public:
    explicit AiPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;
    void askAbout(const QString& question, const QString& game);

signals:
    void toStudio(const QString& kind, const QString& game, const QString& content);

private:
    void send();
    void render();
    void loadSetup();
    void saveSetup();
    void newChat();
    QString historyJson() const;
    void saveTurns() const;

    QGroupBox* setup_;
    QComboBox *provider_, *model_, *game_;
    QLineEdit *key_, *base_;
    QLabel *setupResult_, *status_, *pill_, *attachLbl_;
    QTextBrowser* chat_;
    QListWidget* shots_;
    QPlainTextEdit* input_;
    QCheckBox* web_;
    QPushButton *sendBtn_, *stopBtn_, *saveBtn_, *discordBtn_;
    QStringList attachments_;
    hunter::Json turns_;          // [{q, a, game, sources}] — same file as the Python edition
    QString md_, live_, liveLines_, lastQuestion_, lastAnswer_;
    hunter::Json lastResult_;
    int requestId_ = 0;
    QTimer renderTimer_;
};

class BuildsPage : public Page {
    Q_OBJECT
public:
    explicit BuildsPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;
    void setGame(const QString& game);

signals:
    void toStudio(const QString& kind, const QString& game, const QString& content);
    void askAi(const QString& question, const QString& game);

private:
    void showBuild();
    void showResearched();
    void runResearch(const QString& kind);
    void refreshMine();
    void editMine(hunter::Json build);
    QString game() const;
    QComboBox* game_;
    QListWidget* mine_;
    QTextBrowser* mineDetail_;
    hunter::Json mineRows_;
    QLabel *season_, *researchInfo_;
    QListWidget* builds_;
    QTextBrowser* buildDetail_;
    QTableWidget* researched_;
    QTextBrowser* researchedDetail_;
    QTextBrowser* settings_;
    QTableWidget* researchedSettings_;
    QPlainTextEdit* log_;
    QCheckBox* useAi_;
    QPushButton *rBuilds_, *rSettings_, *stop_;
    hunter::Json curated_, researchedRows_;
};

class ScreenshotsPage : public Page {
    Q_OBJECT
public:
    explicit ScreenshotsPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

signals:
    void toStudioShots(const QString& game, const QStringList& files);

private:
    hunter::Json selected() const;
    QComboBox* game_;
    QLineEdit* weapon_;
    QListWidget* grid_;
    QLabel* info_;
    std::vector<hunter::Json> items_;
};

class StudioPage : public Page {
    Q_OBJECT
public:
    explicit StudioPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;
    void preset(const QString& kind, const QString& game, const QString& content, const QStringList& files = {});

private:
    void reloadItems();
    hunter::Json chosen(std::vector<long long>* codeIds = nullptr) const;
    void preview();
    void send();
    QComboBox *kind_, *game_, *dest_;
    QListWidget* items_;
    QPlainTextEdit *content_, *preview_;
    QLineEdit* customUrl_;
    QCheckBox* mention_;
    std::vector<hunter::Json> objs_;
    QStringList onlyFiles_;
};
