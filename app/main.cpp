// COD Redemption Hunter — native C++ / Qt 6 desktop app.
//   CODRedemptionHunter               open the window
//   CODRedemptionHunter --minimized   start in the tray (used by "Start with Windows")
//   CODRedemptionHunter --scan-once   one scan + check + Discord, print a summary, exit (no window)
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QSplashScreen>
#include <QTimer>
#include <QStandardPaths>
#include <cstdio>
#include <memory>

#include "../core/engine.hpp"
#include "mainwindow.hpp"
#include "qthttp.hpp"
#include "renderer.hpp"
#include "art.hpp"
#include "theme.hpp"
#include "uitranslate.hpp"

using namespace hunter;

namespace {
const char* kInstanceKey = "cod-redemption-hunter-instance";

#ifdef Q_OS_ANDROID
// Android: the shipped data files are compiled into the app (:/shipped/...). Copy them out once per version
// so the rest of the code can read plain files like on the desktop.
fs::path androidShipped() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = base + "/shipped";
    const QString stamp = dir + "/.version";
    QFile vf(stamp);
    if (vf.open(QIODevice::ReadOnly) && vf.readAll() == HUNTER_VERSION) return dir.toStdString();
    vf.close();
    QDirIterator it(":/shipped", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        if (QFileInfo(src).isDir()) continue;
        const QString dst = dir + src.mid(QStringLiteral(":/shipped").size());
        QDir().mkpath(QFileInfo(dst).path());
        QFile::remove(dst);
        QFile::copy(src, dst);
        QFile::setPermissions(dst, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    if (vf.open(QIODevice::WriteOnly | QIODevice::Truncate)) vf.write(HUNTER_VERSION);
    return dir.toStdString();
}
#endif

// Shipped files live next to the exe in data/ (installed) or in the source tree's data/ (dev builds).
fs::path shippedDir() {
#ifdef Q_OS_ANDROID
    return androidShipped();
#endif
    fs::path exeDir = QCoreApplication::applicationDirPath().toStdWString();
    for (const fs::path& p : {exeDir / "data", exeDir.parent_path() / "data", exeDir.parent_path().parent_path() / "data"})
        if (fs::exists(p / "config.json")) return p;
#ifdef HUNTER_SOURCE_DIR
    return fs::path(HUNTER_SOURCE_DIR) / "data";
#else
    return exeDir / "data";
#endif
}

// A second launch just brings the running window to the front.
bool signalRunningInstance() {
    QLocalSocket s;
    s.connectToServer(kInstanceKey);
    if (!s.waitForConnected(300)) return false;
    s.write("show");
    s.waitForBytesWritten(300);
    return true;
}
}  // namespace

// Tactical loading screen (original art: emblem, camo, corner brackets).
QPixmap splashArt(qreal dpr) {
    const Palette& p = theme::current();
    const QSize sz(680, 360);
    QPixmap pm(sz * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(p.bg);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    g.drawPixmap(0, 0, art::pattern(p.pattern == "none" ? QStringLiteral("digital") : p.pattern, sz, p, 85, 42, dpr));
    QLinearGradient shade(0, 0, 0, sz.height());
    shade.setColorAt(0, QColor(0, 0, 0, 40));
    shade.setColorAt(1, QColor(0, 0, 0, 170));
    g.fillRect(QRect(QPoint(), sz), shade);
    QColor wf = p.accent;
    wf.setAlpha(40);
    QColor we = p.accent;
    we.setAlpha(120);
    art::drawWeapon(g, p.weapon == "none" ? QStringLiteral("ar") : p.weapon, QRectF(250, 40, 420, 126), wf, we);
    art::drawEmblem(g, QRectF(44, 120, 96, 96), p);
    QFont t(theme::headingFont());
    t.setPointSizeF(26);
    t.setBold(true);
    t.setCapitalization(QFont::AllUppercase);
    t.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    g.setFont(t);
    g.setPen(p.text);
    g.drawText(QRectF(160, 128, 500, 44), Qt::AlignLeft | Qt::AlignVCenter, "COD Redemption Hunter");
    QFont k(theme::headingFont());
    k.setPointSizeF(10);
    k.setBold(true);
    k.setLetterSpacing(QFont::AbsoluteSpacing, 3);
    g.setFont(k);
    g.setPen(p.accent);
    g.drawText(QRectF(162, 172, 500, 24), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("INTEL // CODES · LOADOUTS · STATS   v") + HUNTER_VERSION);
    g.fillRect(QRectF(44, 300, 592, 4), p.surface2);
    g.fillRect(QRectF(44, 300, 380, 4), p.accent);
    g.setPen(p.muted);
    g.drawText(QRectF(44, 310, 592, 24), Qt::AlignLeft | Qt::AlignVCenter, "LOADING INTEL…");
    QColor cc = p.accent;
    cc.setAlpha(200);
    art::drawCorners(g, QRectF(14, 14, sz.width() - 28, sz.height() - 28), cc, 22, 2.5);
    art::drawChevrons(g, QRectF(sz.width() - 74, sz.height() - 92, 34, 44), cc, 3);
    g.setPen(QPen(p.border, 1));
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(0.5, 0.5, sz.width() - 1, sz.height() - 1));
    return pm;
}

int main(int argc, char** argv) {
#ifdef HUNTER_WITH_WEBENGINE
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);  // required by Qt WebEngine
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName("COD Redemption Hunter");
    QApplication::setOrganizationName("COD Redemption Hunter");
    QApplication::setApplicationVersion(HUNTER_VERSION);
    QApplication::setQuitOnLastWindowClosed(false);  // the tray keeps it alive
    QApplication::setWindowIcon(QIcon(":/app.png"));  // replaced by the themed emblem once a theme is applied

    QCommandLineParser cli;
    cli.setApplicationDescription("Finds Call of Duty redeem codes around the clock.");
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption minimized("minimized", "Start in the system tray.");
    QCommandLineOption scanOnce("scan-once", "Scan once, print a summary and exit.");
    cli.addOption(minimized);
    cli.addOption(scanOnce);
    cli.process(app);

#ifdef Q_OS_ANDROID
    Paths paths{shippedDir(), fs::path(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString()) / "userdata"};
    if (!qEnvironmentVariableIsEmpty("HUNTER_DATA_DIR")) paths.dataDir = Paths::defaultDataDir();
#else
    Paths paths{shippedDir(), Paths::defaultDataDir()};
#endif
    QtHttpClient http;

    if (cli.isSet(scanOnce)) {
        try {
            Engine engine(paths, http);
            CycleResult r = engine.runCycle(nullptr, true, [](const std::string& name, int i, int n) {
                std::printf("[%d/%d] %s\n", i + 1, n, name.c_str());
                std::fflush(stdout);
            });
            std::printf("\n%d sources read, %d errors, %d new codes, %d checked, %d posted to Discord%s\n", r.scan.sources,
                        r.scan.errors, int(r.scan.newCodes.size()), r.checked, r.discord.posted,
                        r.discord.error.empty() ? "" : (" (" + r.discord.error + ")").c_str());
            for (const auto& c : r.scan.newCodes) std::printf("  NEW  %s\n", c.c_str());
            return r.scan.errors && !r.scan.sources ? 1 : 0;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 2;
        }
    }

    if (signalRunningInstance()) return 0;
    fs::create_directories(paths.dataDir);
    QLockFile lock(QString::fromStdWString((paths.dataDir / "app.lock").wstring()));
    if (!lock.tryLock(200)) {
        QMessageBox::information(nullptr, "COD Redemption Hunter", "COD Redemption Hunter is already running (look in the tray).");
        return 0;
    }

    std::unique_ptr<Engine> engine;
    try {
        engine = std::make_unique<Engine>(paths, http);
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "COD Redemption Hunter",
                              QStringLiteral("Couldn't open your data in %1:\n\n%2")
                                  .arg(QString::fromStdWString(paths.dataDir.wstring()), e.what()));
        return 1;
    }
    theme::init(QString::fromStdWString((paths.dataDir / "themes").wstring()));
    theme::apply(QString::fromStdString(engine->config.runtime("theme").toString("warzone")));
    ui18n::install(paths, engine->config.env("APP_LANGUAGE", "en"));  // translates every window as it's shown
    std::unique_ptr<QSplashScreen> splash;
    if (!cli.isSet(minimized)) {
        splash = std::make_unique<QSplashScreen>(splashArt(app.devicePixelRatio()));
        splash->show();
        app.processEvents();
    }
    // JavaScript-built code pages are drawn by the built-in browser (when this build has one)
    PageRenderer renderer(QString::fromStdString(jstr(engine->config.snapshot().get("discovery"), "user_agent")));
#ifdef HUNTER_WITH_WEBENGINE
    engine->discovery.renderer = [&renderer](const std::string& url) { return renderer.renderBlocking(url); };
    engine->renderer = engine->discovery.renderer;  // patch notes on JavaScript pages
#endif
    MainWindow window(*engine, cli.isSet(minimized), &renderer);
    if (splash) QTimer::singleShot(700, splash.get(), [&splash, &window] { if (splash) splash->finish(&window); });

    QLocalServer::removeServer(kInstanceKey);
    QLocalServer server;
    server.listen(kInstanceKey);
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        if (auto* c = server.nextPendingConnection()) c->deleteLater();
        window.showFromTray();
    });
    return app.exec();
}
