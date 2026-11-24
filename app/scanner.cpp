#include "scanner.hpp"

#include <QMetaObject>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace hunter;

void ScannerWorker::runCycle(bool force) {
    stopFlag = false;
    emit cycleStarted();
    QStringList fresh;
    int sources = 0, errors = 0;
    QString discordError;
    try {
        CycleResult r = engine_.runCycle(&stopFlag, force, [this](const std::string& name, int i, int n) {
            emit progress(QString::fromStdString(name), i, n);
        });
        for (const auto& c : r.scan.newCodes) fresh << QString::fromStdString(c);
        sources = r.scan.sources;
        errors = r.scan.errors;
        discordError = QString::fromStdString(r.discord.error);
        // patch watch (every few hours) and community refresh / follow pushes — cheap when not due
        if (!stopFlag) engine_.runExtras([this](const std::string& line) { engine_.store.logScan("Extras", "INFO", line); });
    } catch (const std::exception& e) {
        errors += 1;
        engine_.store.logScan("Scanner", "ERROR", e.what());
        emit message(QStringLiteral("Scan failed: %1").arg(e.what()), false);
    }
    emit cycleFinished(fresh, sources, errors, discordError);
}

void ScannerWorker::testDiscord(const QString& url) {
    DiscordNotifier d(engine_.config, engine_.store, engine_.http);
    auto [ok, msg] = d.test(url.toStdString());
    emit message(QString::fromStdString(msg), ok);
}

void ScannerWorker::postList(bool includeDead) {
    checkAll(engine_.store);
    DiscordNotifier d(engine_.config, engine_.store, engine_.http);
    auto [n, err] = d.postList(includeDead);
    emit message(err.empty() ? QStringLiteral("Posted %1 code(s) to Discord.").arg(n)
                             : QString::fromStdString(err), err.empty());
}

Scanner::Scanner(Engine& engine, QObject* parent) : QObject(parent), engine_(engine), worker_(new ScannerWorker(engine)) {
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &Scanner::requestCycle, worker_, &ScannerWorker::runCycle, Qt::QueuedConnection);
    connect(this, &Scanner::requestTest, worker_, &ScannerWorker::testDiscord, Qt::QueuedConnection);
    connect(this, &Scanner::requestList, worker_, &ScannerWorker::postList, Qt::QueuedConnection);
    connect(worker_, &ScannerWorker::progress, this, &Scanner::progress);
    connect(worker_, &ScannerWorker::message, this, &Scanner::message);
    connect(worker_, &ScannerWorker::cycleStarted, this, [this] { busy_ = true; emit stateChanged(); });
    connect(worker_, &ScannerWorker::cycleFinished, this,
            [this](const QStringList& n, int s, int e, const QString& d) {
                busy_ = false;
                emit cycleFinished(n, s, e, d);
                emit stateChanged();
                if (pending_) { pending_ = false; scanNow(true); }
            });
    thread_.setObjectName("scanner");
    thread_.start();
    timer_.setTimerType(Qt::VeryCoarseTimer);
    connect(&timer_, &QTimer::timeout, this, &Scanner::tick);
}

Scanner::~Scanner() {
    worker_->stopFlag = true;
    thread_.quit();
    thread_.wait(15000);
}

int Scanner::secondsToNext() const { return timer_.isActive() ? timer_.remainingTime() / 1000 : -1; }

void Scanner::start() {
    timer_.start(engine_.config.pollIntervalSeconds() * 1000);
#ifdef Q_OS_WIN
    if (engine_.config.runtime("keep_awake").toBool(true))
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);  // keep scanning while the PC is idle
#endif
    engine_.store.addAudit("MONITOR_STARTED", "every " + std::to_string(engine_.config.pollIntervalSeconds() / 60) + " min");
    emit stateChanged();
    scanNow(false);
}

void Scanner::stop() {
    timer_.stop();
    worker_->stopFlag = true;
#ifdef Q_OS_WIN
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
    engine_.store.addAudit("MONITOR_STOPPED", "");
    emit stateChanged();
}

void Scanner::tick() {
    timer_.setInterval(engine_.config.pollIntervalSeconds() * 1000);  // picks up a changed interval
    scanNow(false);
}

void Scanner::scanNow(bool force) {
    if (busy_) { pending_ = force; return; }
    busy_ = true;
    emit stateChanged();
    emit requestCycle(force);
}

void Scanner::abortCycle() { worker_->stopFlag = true; }
void Scanner::testDiscord(const QString& url) { emit requestTest(url); }
void Scanner::postList(bool includeDead) { emit requestList(includeDead); }
