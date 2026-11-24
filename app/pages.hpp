// The window's pages: Codes (per game or all), Sources, Activity, Discord, Settings.
#pragma once
#include <QMap>
#include <QWidget>

#include "../core/engine.hpp"
#include "scanner.hpp"

class QButtonGroup;
class QGroupBox;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSortFilterProxyModel;
class QSpinBox;
class QTableView;
class QTableWidget;
class QTabWidget;
class CodesModel;

class PageRenderer;
class AiService;

struct AppContext {
    hunter::Engine& engine;
    Scanner& scanner;
    QWidget* window;
    PageRenderer* renderer = nullptr;  // built-in browser helpers (may be a no-op build)
    AiService* ai = nullptr;            // AI chat / research / studio worker thread
};

class Page : public QWidget {
    Q_OBJECT
public:
    explicit Page(AppContext& ctx, QWidget* parent = nullptr) : QWidget(parent), ctx_(ctx) {}
    virtual void refresh() {}

protected:
    AppContext& ctx_;
};

class CodesPage : public Page {
    Q_OBJECT
public:
    explicit CodesPage(AppContext& ctx, QWidget* parent = nullptr);
    void setGame(const QString& gameId);   // "" = every game
    void setView(const QString& view);
    void refresh() override;
    void focusSearch();
    void searchFor(const QString& text);
    void redeemSelected();

signals:
    void changed();  // a code's status changed here (counts need updating)

private:
    void showDetail();
    long long selectedId() const;
    void mark(const QString& status);
    void addManual();
    void exportCsv();
    QString game_, view_ = "ready";
    QLabel* title_;
    QLabel* hint_;
    QLineEdit* search_;
    QButtonGroup* chips_;
    QTableView* table_;
    CodesModel* model_;
    QSortFilterProxyModel* proxy_;
    QLabel* empty_;
    // detail panel
    QWidget* detail_;
    QLabel *dCode_, *dMeta_, *dVerdict_, *dReward_, *dReasons_, *dHow_;
    QListWidget* dSightings_;
    QPushButton *dRedeem_, *dCopy_;
};

class SourcesPage : public Page {
    Q_OBJECT
public:
    explicit SourcesPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

private:
    void addSource();
    void editSource(int row);
    bool sourceDialog(hunter::Json& src, const QString& title);
    void exportSources();
    void importSources();
    void applyFilter();
    void removeSelected();
    void toggled(int row, bool on);
    QTableWidget* table_;
    QLabel* summary_;
    QLineEdit* filter_;
    bool loading_ = false;
};

class ActivityPage : public Page {
    Q_OBJECT
public:
    explicit ActivityPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

private:
    QTableWidget *scans_, *audit_, *attempts_;
};

class DiscordPage : public Page {
    Q_OBJECT
public:
    explicit DiscordPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

private:
    void save();
    struct GameRow {
        std::string id;
        QCheckBox* post;
        QLineEdit* hook;
    };
    std::vector<GameRow> games_;
    QLineEdit *webhook_, *mention_, *username_, *systemHook_;
    QComboBox* which_;
    QCheckBox *enabled_, *postNew_, *updates_, *garena_, *onConnect_;
};

class AccountPage : public Page {
    Q_OBJECT
public:
    explicit AccountPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

signals:
    void openSettings();

private:
    void testPage(const QString& region);
    QLabel *uid_, *binding_, *limits_, *breaker_, *pageResult_;
    QTableWidget* history_;
};

class SettingsPage : public Page {
    Q_OBJECT
public:
    explicit SettingsPage(AppContext& ctx, QWidget* parent = nullptr);
    void refresh() override;

    void showThemes();  // scroll to the theme gallery

signals:
    void themeChosen(const QString& name);

private:
    void save();
    void updateLanguageInfo();
    void rebuildThemeTiles();
    void loadScoring();
    QLineEdit *uid_, *urlGlobal_, *urlGarena_;
    QComboBox* region_;
    QSpinBox* interval_;
    QCheckBox *onLaunch_, *autostart_, *tray_, *notify_, *awake_;
    QLabel *protection_, *dataPath_;
    QComboBox* language_;
    QScrollArea* scroll_ = nullptr;
    QGroupBox* looks_ = nullptr;
    QWidget* tiles_ = nullptr;
    QPushButton* deleteTheme_ = nullptr;
    QMap<QString, QSpinBox*> scoring_;
    QPlainTextEdit* blocklist_ = nullptr;
    QLabel *langInfo_, *pushInfo_;
    QCheckBox *pushCodes_, *pushPatch_, *pushCommunity_;
};
