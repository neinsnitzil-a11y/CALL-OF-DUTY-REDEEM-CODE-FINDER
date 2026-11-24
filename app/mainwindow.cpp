#include "mainwindow.hpp"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <QPainter>

#include "../core/checker.hpp"
#include "../core/games.hpp"
#include "../core/util.hpp"
#include "art.hpp"
#include "commandpalette.hpp"
#include "renderer.hpp"
#include "uitranslate.hpp"
#include "theme.hpp"
#include "widgets.hpp"
#include "wizard.hpp"

using namespace hunter;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

class Emblem : public QWidget {  // the app mark, drawn in the theme's colours
public:
    explicit Emblem(QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(40, 40); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        art::drawEmblem(p, QRectF(rect()), theme::current());
    }
};

QIcon navIcon(const QString& name) {
    const Palette& p = theme::current();
    QIcon ic;
    for (int sz : {18, 20, 24})
        for (qreal dpr : {1.0, 2.0}) {
            ic.addPixmap(art::iconPixmap(name, p.muted, sz, dpr), QIcon::Normal);
            ic.addPixmap(art::iconPixmap(name, p.accent, sz, dpr), QIcon::Selected);
            ic.addPixmap(art::iconPixmap(name, p.text, sz, dpr), QIcon::Active);
        }
    return ic;
}
}

MainWindow::MainWindow(Engine& engine, bool startHidden, PageRenderer* renderer, QWidget* parent)
    : QMainWindow(parent), engine_(engine), scanner_(engine), aiService_(engine), ctx_{engine, scanner_, this, renderer, &aiService_} {
    if (renderer)  // research reads JavaScript-built guide pages with the built-in browser too
        aiService_.worker()->renderer = [renderer](const std::string& url) { return renderer->renderBlocking(url); };
    setWindowTitle("COD Redemption Hunter");
    resize(1360, 860);
    setMinimumSize(1080, 680);

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildAppBar());
    root->addWidget(buildStats());
    auto* body = new QHBoxLayout;
    body->setSpacing(0);
    body->addWidget(buildNav());
    stack_ = new QStackedWidget;
    codes_ = new CodesPage(ctx_);
    sources_ = new SourcesPage(ctx_);
    activity_ = new ActivityPage(ctx_);
    discord_ = new DiscordPage(ctx_);
    settings_ = new SettingsPage(ctx_);
    account_ = new AccountPage(ctx_);
    ai_ = new AiPage(ctx_);
    builds_ = new BuildsPage(ctx_);
    shots_ = new ScreenshotsPage(ctx_);
    studio_ = new StudioPage(ctx_);
    stats_ = new StatsPage(ctx_);
    foryou_ = new ForYouPage(ctx_);
    community_ = new CommunityPage(ctx_);
    for (QWidget* p : std::initializer_list<QWidget*>{codes_, sources_, activity_, discord_, settings_, account_, ai_, builds_, shots_, studio_, stats_, foryou_, community_}) stack_->addWidget(p);
    body->addWidget(stack_, 1);
    root->addLayout(body, 1);
    root->addWidget(buildStatusBar());
    setCentralWidget(central);
    buildTray();

    connect(codes_, &CodesPage::changed, this, &MainWindow::refreshStatus);
    connect(settings_, &SettingsPage::themeChosen, this, &MainWindow::setTheme);
    connect(account_, &AccountPage::openSettings, this, [this] { navigate("settings"); });
    auto toStudio = [this](const QString& kind, const QString& game, const QString& content) {
        navigate("studio");
        studio_->preset(kind, game, content);
    };
    connect(ai_, &AiPage::toStudio, this, toStudio);
    connect(builds_, &BuildsPage::toStudio, this, toStudio);
    connect(builds_, &BuildsPage::askAi, this, [this](const QString& question, const QString& game) {
        navigate("ai");
        ai_->askAbout(question, game);
    });
    connect(shots_, &ScreenshotsPage::toStudioShots, this, [this](const QString& game, const QStringList& files) {
        navigate("studio");
        studio_->preset("shots", game, "", files);
    });
    connect(&scanner_, &Scanner::stateChanged, this, &MainWindow::refreshStatus);
    connect(&scanner_, &Scanner::progress, this, [this](const QString& name, int i, int n) {
        progress_->setRange(0, n);
        progress_->setValue(i + 1);
        statusText_->setText(QStringLiteral("Reading %1  (%2/%3)").arg(name).arg(i + 1).arg(n));
    });
    connect(&scanner_, &Scanner::cycleFinished, this, &MainWindow::onCycleFinished);
    connect(&scanner_, &Scanner::message, this, [this](const QString& text, bool ok) { Toast::popup(this, text, ok, 5000); });

    auto* clock = new QTimer(this);  // countdown + live counts
    connect(clock, &QTimer::timeout, this, &MainWindow::refreshStatus);
    clock->start(1000);

    // keyboard
    auto key = [this](const QKeySequence& k, auto fn) { connect(new QShortcut(k, this), &QShortcut::activated, this, fn); };
    key(QKeySequence(Qt::Key_F5), [this] { scanner_.scanNow(true); });
    key(QKeySequence::Find, [this] { navigate("all"); codes_->focusSearch(); });
    key(QKeySequence(Qt::CTRL | Qt::Key_T), [this] {
        auto n = theme::names();
        setTheme(n[(n.indexOf(theme::current().name) + 1) % n.size()]);
    });
    key(QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { quit(); });
    key(QKeySequence(Qt::CTRL | Qt::Key_I), [this] { navigate("ai"); });
    key(QKeySequence(Qt::CTRL | Qt::Key_K), [this] { openPalette(); });
    key(QKeySequence(Qt::CTRL | Qt::Key_B), [this] { setNavCollapsed(!navCollapsed_); });
    if (engine_.config.runtime("nav_collapsed").toBool(false)) setNavCollapsed(true);
    retintChrome();

    navigate("all");
    refreshAll();
    if (engine_.store.getState("setup_complete") != "1") QTimer::singleShot(300, this, &MainWindow::runWizard);
    else if (engine_.config.runtime("start_monitoring_on_launch").toBool(true)) QTimer::singleShot(600, &scanner_, &Scanner::start);
    if (!startHidden || !tray_) show();
}

// ── app bar ─────────────────────────────────────────────────────
QWidget* MainWindow::buildAppBar() {
    auto* bar = new TacticalFrame("AppBar", TacticalFrame::FadeRight, 70);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 10, 14, 10);
    h->setSpacing(8);
    h->addWidget(new Emblem);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = label("COD Redemption Hunter", "AppTitle");
    QFont tf = title->font();
    tf.setCapitalization(QFont::AllUppercase);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    title->setFont(tf);
    title->setTextInteractionFlags(Qt::NoTextInteraction);
    titles->addWidget(title);
    titles->addWidget(label(QStringLiteral("v%1 · every Call of Duty · native C++").arg(q(engine_.config.version())), "AppSub"));
    h->addLayout(titles);
    h->addSpacing(18);
    auto* omni = iconButton("search", "Search or jump to…        Ctrl+K");
    omni->setObjectName("Omni");
    omni->setToolTip("Pages, actions, themes and codes (Ctrl+K)");
    connect(omni, &QPushButton::clicked, this, [this] { openPalette(); });
    h->addWidget(omni);
    h->addStretch(1);
    pill_ = new QLabel("● Stopped");
    pill_->setObjectName("Pill");
    h->addWidget(pill_);
    h->addSpacing(6);
    startBtn_ = iconButton("play", "Start scanning", "accent");
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::toggleMonitoring);
    auto* scan = iconButton("refresh", "Scan now", "ghost");
    scan->setToolTip("Read every source right now (F5)");
    connect(scan, &QPushButton::clicked, this, [this] { scanner_.scanNow(true); });
    themeBtn_ = new QToolButton;
    themeBtn_->setObjectName("ThemeBtn");
    themeBtn_->setToolTip("Theme (Ctrl+T cycles)");
    themeBtn_->setPopupMode(QToolButton::InstantPopup);
    auto* themeMenu = new QMenu(themeBtn_);
    connect(themeMenu, &QMenu::aboutToShow, this, [this, themeMenu] {
        themeMenu->clear();
        for (const auto& n : theme::names()) {
            Palette p = theme::get(n);
            QAction* a = themeMenu->addAction(art::icon("palette", p.accent), p.label, this, [this, n] { setTheme(n); });
            a->setCheckable(true);
            a->setChecked(theme::current().name == n);
        }
        themeMenu->addSeparator();
        themeMenu->addAction(ui18n::tr("Customize themes…"), this, [this] { navigate("settings"); settings_->showThemes(); });
    });
    themeBtn_->setMenu(themeMenu);
    auto* stop = iconButton("warning", "Emergency stop", "danger");
    stop->setToolTip("Stops scanning and blocks all redemption until you clear it in Settings");
    connect(stop, &QPushButton::clicked, this, &MainWindow::emergencyStop);
    moreBtn_ = new QToolButton;
    moreBtn_->setObjectName("MoreBtn");
    moreBtn_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(moreBtn_);
    menu->addAction("Setup wizard…", this, &MainWindow::runWizard);
    menu->addAction("Command palette…   Ctrl+K", this, [this] { openPalette(); });
    menu->addAction("Open data folder", this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(q(engine_.config.paths().dataDir.string())));
    });
    menu->addAction("Check every code now", this, [this] {
        int n = checkAll(engine_.store);
        Toast::popup(this, QStringLiteral("Checked %1 codes.").arg(n));
        refreshAll();
    });
    menu->addSeparator();
    menu->addAction("About", this, &MainWindow::about);
    menu->addAction("Quit", this, &MainWindow::quit);
    moreBtn_->setMenu(menu);
    for (QWidget* w : std::initializer_list<QWidget*>{startBtn_, scan, themeBtn_, stop, moreBtn_}) h->addWidget(w);
    return bar;
}

void MainWindow::retintChrome() {
    const Palette& p = theme::current();
    themeBtn_->setIcon(art::icon("palette", p.text));
    moreBtn_->setIcon(art::icon("more", p.text));
    navToggle_->setIcon(art::icon(navCollapsed_ ? "chevron-right" : "chevron-left", p.muted));
    for (int i = 0; i < nav_->count(); ++i) {
        const QString ic = nav_->item(i)->data(Qt::UserRole + 3).toString();
        if (!ic.isEmpty()) nav_->item(i)->setIcon(navIcon(ic));
    }
    setWindowIcon(art::emblemIcon(p));
    if (tray_) tray_->setIcon(art::emblemIcon(p));
    retintIcons(this);
}

void MainWindow::openPalette() {
    if (!palette_) {
        palette_ = new CommandPalette(this);
        palette_->setSearchFallback([this](const QString& t) { navigate("all"); codes_->searchFor(t); });
    }
    std::vector<CommandPalette::Command> cmds;
    for (int i = 0; i < nav_->count(); ++i) {
        QListWidgetItem* it = nav_->item(i);
        if (it->flags() == Qt::NoItemFlags) continue;
        const QString key = it->data(Qt::UserRole).toString();
        cmds.push_back({it->data(Qt::UserRole + 3).toString(), it->data(Qt::UserRole + 1).toString().trimmed(), ui18n::tr("page"),
                        [this, key] { navigate(key); }});
    }
    cmds.push_back({"refresh", "Scan now", "F5", [this] { scanner_.scanNow(true); }});
    cmds.push_back({scanner_.running() ? "stop" : "play", scanner_.running() ? "Stop scanning" : "Start scanning", "",
                    [this] { toggleMonitoring(); }});
    cmds.push_back({"check", "Check every code now", "", [this] {
                        int n = checkAll(engine_.store);
                        Toast::popup(this, QStringLiteral("Checked %1 codes.").arg(n));
                        refreshAll();
                    }});
    cmds.push_back({"codes", "Show codes ready to redeem", "", [this] { navigate("all"); codes_->setView("ready"); }});
    cmds.push_back({"plus", "Add a source", "", [this] { navigate("sources"); }});
    cmds.push_back({"bolt", "Setup wizard", "", [this] { runWizard(); }});
    cmds.push_back({"folder", "Open data folder", "", [this] {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(q(engine_.config.paths().dataDir.string())));
                    }});
    cmds.push_back({"sliders", "Customize themes", "", [this] { navigate("settings"); settings_->showThemes(); }});
    for (const auto& n : theme::names()) {
        Palette p = theme::get(n);
        cmds.push_back({"palette", ui18n::tr("Theme") + ": " + p.label, "", [this, n] { setTheme(n); }});
    }
    cmds.push_back({"warning", "Emergency stop", "", [this] { emergencyStop(); }});
    palette_->setCommands(std::move(cmds));
    palette_->openWith();
}

QWidget* MainWindow::buildStats() {
    auto* strip = new QWidget;
    auto* h = new QHBoxLayout(strip);
    h->setContentsMargins(18, 14, 18, 4);
    h->setSpacing(12);
    const Palette& p = theme::current();
    const std::tuple<const char*, const char*, QColor, const char*, const char*> defs[] = {
        {"ready", "Ready to redeem", p.green, "ready", "crosshair"}, {"fresh", "New since you started", p.accent, "fresh", "bolt"},
        {"maybe", "Unconfirmed", p.yellow, "maybe", "eye"}, {"redeemed", "Redeemed", p.blue, "redeemed", "trophy"},
        {"expired", "Expired / used up", p.grey, "expired", "clock"}};
    for (auto& [key, title, color, view, icon] : defs) {
        auto* c = new StatCard(title, color, icon);
        QString v = view;
        connect(c, &StatCard::clicked, this, [this, v] { navigate("all"); codes_->setView(v); });
        cards_[key] = c;
        h->addWidget(c, 1);
    }
    return strip;
}

QWidget* MainWindow::buildNav() {
    auto* rail = new TacticalFrame("NavRail", TacticalFrame::FadeUp, 60);
    rail->setFixedWidth(248);
    navRail_ = rail;
    auto* v = new QVBoxLayout(rail);
    v->setContentsMargins(0, 8, 0, 0);
    v->setSpacing(0);
    nav_ = new QListWidget;
    nav_->setObjectName("Nav");
    nav_->setFocusPolicy(Qt::NoFocus);
    nav_->setIconSize(QSize(18, 18));
    {
        QFont nf = nav_->font();
        nf.setFamily(theme::headingFont());
        nf.setCapitalization(QFont::AllUppercase);
        nf.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        nav_->setFont(nf);
    }
    auto section = [this](const QString& text, const QString& key = QString()) {
        auto* it = new QListWidgetItem(text.toUpper());
        it->setFlags(Qt::NoItemFlags);
        it->setData(Qt::UserRole + 1, text.toUpper());
        it->setData(Qt::UserRole + 2, key.isEmpty() ? QStringLiteral("section") : key);  // sections can hide when empty
        nav_->addItem(it);
    };
    auto item = [this](const QString& text, const QString& key, const QString& icon) {
        auto* it = new QListWidgetItem(text);
        it->setData(Qt::UserRole, key);
        it->setData(Qt::UserRole + 1, text);  // label without the count badge
        it->setData(Qt::UserRole + 3, icon);
        nav_->addItem(it);
    };
    section("Codes");
    item("All games", "all", "crosshair");
    for (const auto& id : currentTitles()) item(q(gameById(id).label()), "game:" + q(id), "target");
    section("Older titles", "older");
    for (const auto& g : allGames())
        if (g.id != "other" && std::find(currentTitles().begin(), currentTitles().end(), g.id) == currentTitles().end())
            item(q(g.label()), "game:" + q(g.id), "dogtag");
    item("Other / unsorted", "game:other", "layers");
    section("Armory");
    item("For you", "foryou", "star");
    item("Builds & settings", "builds", "rifle");
    item("Stats & patches", "stats", "chart");
    item("Community", "community", "users");
    item("AI helper", "ai", "bot");
    item("Screenshots", "shots", "image");
    item("Discord studio", "studio", "brush");
    section("Manage");
    item("Sources", "sources", "globe");
    item("Discord", "discord", "megaphone");
    item("Account", "account", "shield");
    item("Activity", "activity", "pulse");
    item("Settings", "settings", "gear");
    connect(nav_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (it && it->flags() != Qt::NoItemFlags) navigate(it->data(Qt::UserRole).toString());
    });
    v->addWidget(nav_, 1);

    auto* foot = new QFrame;
    foot->setObjectName("RailFooter");
    auto* fh = new QHBoxLayout(foot);
    fh->setContentsMargins(14, 8, 10, 8);
    railVersion_ = label(QStringLiteral("v%1").arg(q(engine_.config.version())), "Muted");
    fh->addWidget(railVersion_);
    fh->addStretch(1);
    navToggle_ = new QToolButton;
    navToggle_->setObjectName("NavToggle");
    navToggle_->setToolTip("Collapse / expand the side bar");
    connect(navToggle_, &QToolButton::clicked, this, [this] { setNavCollapsed(!navCollapsed_); });
    fh->addWidget(navToggle_);
    v->addWidget(foot);
    return rail;
}

void MainWindow::setNavCollapsed(bool on) {
    navCollapsed_ = on;
    navRail_->setFixedWidth(on ? 66 : 248);
    railVersion_->setVisible(!on);
    for (int i = 0; i < nav_->count(); ++i) {
        QListWidgetItem* it = nav_->item(i);
        if (it->flags() == Qt::NoItemFlags) {
            if (it->data(Qt::UserRole + 2).toString() == "section") it->setHidden(on);
            continue;
        }
        const QString base = ui18n::tr(it->data(Qt::UserRole + 1).toString());
        it->setText(on ? QString() : base);
        it->setToolTip(on ? base : QString());
    }
    Json cfg = engine_.config.snapshot();
    cfg["runtime"]["nav_collapsed"] = on;
    engine_.config.save(cfg);
    retintChrome();
    refreshNavCounts();
}

// "BO7 · 3" badges; older titles only show when they have codes.
void MainWindow::refreshNavCounts() {
    auto counts = engine_.store.gameCounts();
    int readyAll = 0;
    for (auto& [g, c] : counts) readyAll += c.first;
    bool anyOlder = false;
    for (int i = 0; i < nav_->count(); ++i) {
        QListWidgetItem* it = nav_->item(i);
        const QString key = it->data(Qt::UserRole).toString(), base = ui18n::tr(it->data(Qt::UserRole + 1).toString());
        auto show = [this, it](const QString& t) {
            if (navCollapsed_) { it->setText(QString()); it->setToolTip(t); }
            else it->setText(t);
        };
        if (key == "all") {
            show(readyAll ? QStringLiteral("%1   ·  %2 %3").arg(base).arg(readyAll).arg(ui18n::tr("ready")) : base);
            continue;
        }
        if (!key.startsWith("game:")) continue;
        const std::string id = key.mid(5).toStdString();
        auto c = counts.count(id) ? counts[id] : std::pair<int, int>{0, 0};
        show(c.first ? QStringLiteral("%1   ·  %2 %3").arg(base).arg(c.first).arg(ui18n::tr("ready"))
                     : c.second ? QStringLiteral("%1   ·  %2").arg(base).arg(c.second) : base);
        bool current = std::find(currentTitles().begin(), currentTitles().end(), id) != currentTitles().end();
        if (!current) {
            bool visible = c.second > 0 || nav_->currentItem() == it;
            it->setHidden(!visible);
            anyOlder |= visible;
        }
    }
    for (int i = 0; i < nav_->count(); ++i)
        if (nav_->item(i)->data(Qt::UserRole + 2).toString() == "older") nav_->item(i)->setHidden(!anyOlder || navCollapsed_);
}

QWidget* MainWindow::buildStatusBar() {
    auto* bar = new QFrame;
    bar->setObjectName("StatusBar");
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(18, 5, 18, 5);
    h->setSpacing(14);
    statusText_ = new QLabel("Idle");
    progress_ = new QProgressBar;
    progress_->setFixedWidth(160);
    progress_->setVisible(false);
    lastScan_ = new QLabel;
    statusRight_ = new QLabel;
    h->addWidget(statusText_);
    h->addWidget(progress_);
    h->addStretch(1);
    h->addWidget(lastScan_);
    h->addWidget(statusRight_);
    return bar;
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(art::emblemIcon(theme::current()), this);
    tray_->setToolTip("COD Redemption Hunter");
    auto* menu = new QMenu(this);
    menu->addAction("Open", this, &MainWindow::showFromTray);
    menu->addAction("Scan now", this, [this] { scanner_.scanNow(true); });
    trayToggle_ = menu->addAction("Pause scanning", this, &MainWindow::toggleMonitoring);
    menu->addSeparator();
    menu->addAction("Quit", this, &MainWindow::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) showFromTray();
    });
    connect(tray_, &QSystemTrayIcon::messageClicked, this, [this] { showFromTray(); navigate("all"); codes_->setView("ready"); });
    tray_->show();
}

void MainWindow::showFromTray() {
    showNormal();
    raise();
    activateWindow();
}

// ── navigation & refresh ────────────────────────────────────────
void MainWindow::navigate(const QString& key) {
    for (int i = 0; i < nav_->count(); ++i) {
        if (nav_->item(i)->data(Qt::UserRole).toString() == key && nav_->currentRow() != i) {
            QSignalBlocker b(nav_);
            nav_->setCurrentRow(i);
        }
    }
    Page* page = codes_;
    if (key == "all") codes_->setGame("");
    else if (key.startsWith("game:")) codes_->setGame(key.mid(5));
    else if (key == "sources") page = sources_;
    else if (key == "activity") page = activity_;
    else if (key == "discord") page = discord_;
    else if (key == "settings") page = settings_;
    else if (key == "account") page = account_;
    else if (key == "ai") page = ai_;
    else if (key == "builds") page = builds_;
    else if (key == "shots") page = shots_;
    else if (key == "studio") page = studio_;
    else if (key == "stats") page = stats_;
    else if (key == "foryou") page = foryou_;
    else if (key == "community") page = community_;
    stack_->setCurrentWidget(page);
    if (page != codes_) page->refresh();
}

void MainWindow::refreshStatus() {
    if (tick_++ % 5 == 0) refreshNavCounts();
    auto counts = engine_.store.viewCounts();
    for (auto it = cards_.begin(); it != cards_.end(); ++it) it.value()->setValue(counts[it.key().toStdString()]);
    const bool running = scanner_.running(), busy = scanner_.busy();
    const char* state = busy ? "busy" : running ? "on" : "off";
    auto cb = engine_.protection.breaker.state();
    QString pill = busy ? "◌ Scanning…" : running ? "● Watching 24/7" : "○ Stopped";
    if (cb.open) { pill = "▲ Redemption paused"; state = "bad"; }
    if (pill_->text() != pill) {
        pill_->setText(pill);
        pill_->setProperty("state", state);
        repolish(pill_);
    }
    if (running != lastRunning_) {
        lastRunning_ = running;
        startBtn_->setText(ui18n::tr(running ? "Stop scanning" : "Start scanning"));
        startBtn_->setProperty("kind", running ? "" : "accent");
        startBtn_->setProperty("iconName", running ? "stop" : "play");
        repolish(startBtn_);
        retintIcons(startBtn_);
    }
    if (trayToggle_) trayToggle_->setText(running ? "Pause scanning" : "Start scanning");
    progress_->setVisible(busy);
    if (!busy) {
        int next = scanner_.secondsToNext();
        statusText_->setText(running ? QStringLiteral("Next scan in %1:%2").arg(next / 60).arg(next % 60, 2, 10, QChar('0'))
                                     : "Not scanning — press Start scanning");
    }
    Json last = engine_.lastCycle();
    if (!jstr(last, "finished_at").empty()) {
        QDateTime t = QDateTime::fromString(q(jstr(last, "finished_at")), Qt::ISODate).toLocalTime();
        lastScan_->setText(QStringLiteral("Last scan %1 · %2 sources · %3 errors")
                               .arg(t.toString("HH:mm"))
                               .arg(jinteger(last, "sources"))
                               .arg(jinteger(last, "errors")));
    }
    statusRight_->setText(QStringLiteral("%1 sources on · UID %2")
                              .arg(engine_.discovery.enabledSources().size())
                              .arg(q(engine_.protection.binding.masked())));
    if (tray_) tray_->setToolTip(QStringLiteral("COD Redemption Hunter — %1 ready").arg(counts["ready"]));
}

void MainWindow::refreshAll() {
    refreshNavCounts();
    refreshStatus();
    if (auto* p = qobject_cast<Page*>(stack_->currentWidget())) p->refresh();
}

void MainWindow::onCycleFinished(const QStringList& fresh, int sources, int errors, const QString& discordError) {
    refreshAll();
    if (!discordError.isEmpty()) Toast::popup(this, "Discord: " + discordError, false, 6000);
    if (fresh.isEmpty()) return;
    // tell you about new codes that look usable
    QStringList good;
    for (const auto& c : fresh) {
        auto row = engine_.store.getCodeByText(c.toStdString());
        if (row && (row->checkVerdict == "LIKELY WORKS" || row->status == "READY"))
            good << QStringLiteral("%1 (%2)").arg(c, q(gameById(row->game).label()));
    }
    if (good.isEmpty()) return;
    QString text = QStringLiteral("%1 new code%2: %3").arg(good.size()).arg(good.size() > 1 ? QStringLiteral("s") : QString()).arg(good.mid(0, 4).join(", "));
    Toast::popup(this, text, true, 8000);
    if (tray_ && engine_.config.runtime("windows_notifications").toBool(true))
        tray_->showMessage("New Call of Duty codes", text, art::emblemIcon(theme::current()), 10000);
    Q_UNUSED(sources);
    Q_UNUSED(errors);
}

// ── actions ─────────────────────────────────────────────────────
void MainWindow::toggleMonitoring() {
    if (scanner_.running()) {
        scanner_.stop();
        Toast::popup(this, "Scanning stopped.");
    } else {
        scanner_.start();
        Toast::popup(this, QStringLiteral("Scanning every %1 min.").arg(engine_.config.pollIntervalSeconds() / 60));
    }
    refreshStatus();
}

void MainWindow::emergencyStop() {
    scanner_.stop();
    scanner_.abortCycle();
    engine_.emergencyStop();
    for (auto* w : findChildren<QDialog*>()) w->close();  // closes an open redemption window
    Toast::popup(this, "Emergency stop: scanning stopped and redemption blocked. Clear it in Settings.", false, 7000);
    refreshStatus();
}

void MainWindow::setTheme(const QString& name) {
    theme::apply(name);
    retintChrome();
    Json cfg = engine_.config.snapshot();
    cfg["runtime"]["theme"] = name.toStdString();
    engine_.config.save(cfg);
    const Palette& p = theme::current();
    cards_["ready"]->setAccent(p.green);
    cards_["fresh"]->setAccent(p.accent);
    cards_["maybe"]->setAccent(p.yellow);
    cards_["redeemed"]->setAccent(p.blue);
    cards_["expired"]->setAccent(p.grey);
    for (auto* w : findChildren<QWidget*>()) w->update();
    refreshAll();
}

void MainWindow::runWizard() {
    SetupWizard w(engine_, this);
    if (w.exec() == QDialog::Accepted) {
        refreshAll();
        if (w.startScanning() && !scanner_.running()) scanner_.start();
        Toast::popup(this, "All set. New codes will show up here and in your tray.");
    }
}

void MainWindow::about() {
    QMessageBox box(this);
    box.setWindowTitle("About");
    box.setIconPixmap(QIcon(":/app.png").pixmap(64, 64));
    box.setText(QStringLiteral("<h3>COD Redemption Hunter %1</h3>").arg(q(engine_.config.version())));
    box.setInformativeText(
        "Native C++ / Qt edition. Watches public sources for Call of Duty redeem codes, checks them, and opens the official page "
        "with the code filled in — you always solve the CAPTCHA and press Redeem yourself.<br><br>"
        "Not affiliated with Activision, Infinity Ward, Treyarch, TiMi or Garena.<br><br>Your data: " +
        q(engine_.config.paths().dataDir.string()));
    box.exec();
}

void MainWindow::quit() {
    quitting_ = true;
    close();
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!quitting_ && tray_ && engine_.config.runtime("minimize_to_tray").toBool(true)) {
        hide();
        if (!trayHintShown_) {
            tray_->showMessage("Still hunting", "COD Redemption Hunter keeps scanning in the tray. Right-click the icon to quit.",
                               QIcon(":/app.png"), 5000);
            trayHintShown_ = true;
        }
        e->ignore();
        return;
    }
    scanner_.stop();
    e->accept();
    qApp->quit();
}
