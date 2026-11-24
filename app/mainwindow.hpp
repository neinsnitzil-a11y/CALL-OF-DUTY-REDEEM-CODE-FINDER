// The app shell: app bar, stat cards, navigation rail, pages, status bar, tray icon.
#pragma once
#include <QMainWindow>
#include <QMap>
#include <QSystemTrayIcon>

#include "../core/engine.hpp"
#include "aiworker.hpp"
#include "content_pages.hpp"
#include "pages.hpp"
#include "scanner.hpp"
#include "stage4_pages.hpp"

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class StatCard;
class QToolButton;
class QFrame;
class CommandPalette;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(hunter::Engine& engine, bool startHidden, PageRenderer* renderer = nullptr, QWidget* parent = nullptr);
    void showFromTray();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildAppBar();
    QWidget* buildStats();
    QWidget* buildNav();
    QWidget* buildStatusBar();
    void buildTray();
    void navigate(const QString& key);
    void refreshStatus();
    void refreshAll();
    void refreshNavCounts();
    void onCycleFinished(const QStringList& fresh, int sources, int errors, const QString& discordError);
    void toggleMonitoring();
    void emergencyStop();
    void setTheme(const QString& name);
    void runWizard();
    void about();
    void quit();
    void openPalette();
    void retintChrome();
    void setNavCollapsed(bool on);

    hunter::Engine& engine_;
    Scanner scanner_;
    AiService aiService_;
    AppContext ctx_;
    QStackedWidget* stack_;
    QListWidget* nav_;
    CodesPage* codes_;
    SourcesPage* sources_;
    ActivityPage* activity_;
    DiscordPage* discord_;
    SettingsPage* settings_;
    AccountPage* account_;
    AiPage* ai_;
    BuildsPage* builds_;
    ScreenshotsPage* shots_;
    StudioPage* studio_;
    StatsPage* stats_;
    ForYouPage* foryou_;
    CommunityPage* community_;
    QMap<QString, StatCard*> cards_;
    QLabel *pill_, *statusText_, *statusRight_, *lastScan_;
    QProgressBar* progress_;
    QPushButton* startBtn_;
    QToolButton *themeBtn_ = nullptr, *moreBtn_ = nullptr, *navToggle_ = nullptr;
    QFrame* navRail_ = nullptr;
    QLabel* railVersion_ = nullptr;
    CommandPalette* palette_ = nullptr;
    bool navCollapsed_ = false, lastRunning_ = true;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayToggle_ = nullptr;
    bool quitting_ = false, trayHintShown_ = false;
    int tick_ = 0;
};
