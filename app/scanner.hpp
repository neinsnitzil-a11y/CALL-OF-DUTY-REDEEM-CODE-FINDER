// Runs scan cycles on a worker thread on a timer. The window talks to it only through signals
// and queued slots, so the UI never waits on the network.
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <atomic>

#include "../core/engine.hpp"

class ScannerWorker : public QObject {
    Q_OBJECT
public:
    explicit ScannerWorker(hunter::Engine& engine) : engine_(engine) {}
    std::atomic<bool> stopFlag{false};

public slots:
    void runCycle(bool force);
    void testDiscord(const QString& url);
    void postList(bool includeDead);

signals:
    void cycleStarted();
    void progress(const QString& source, int index, int total);
    void cycleFinished(const QStringList& newCodes, int sources, int errors, const QString& discordError);
    void message(const QString& text, bool ok);

private:
    hunter::Engine& engine_;
};

class Scanner : public QObject {
    Q_OBJECT
public:
    explicit Scanner(hunter::Engine& engine, QObject* parent = nullptr);
    ~Scanner() override;

    bool running() const { return timer_.isActive(); }
    bool busy() const { return busy_; }
    int secondsToNext() const;

public slots:
    void start();
    void stop();
    void scanNow(bool force = true);
    void abortCycle();
    void testDiscord(const QString& url);
    void postList(bool includeDead);

signals:
    void stateChanged();
    void progress(const QString& source, int index, int total);
    void cycleFinished(const QStringList& newCodes, int sources, int errors, const QString& discordError);
    void message(const QString& text, bool ok);
    void requestCycle(bool force);      // → worker (queued)
    void requestTest(const QString& url);
    void requestList(bool includeDead);

private:
    void tick();
    hunter::Engine& engine_;
    QThread thread_;
    ScannerWorker* worker_;
    QTimer timer_;
    bool busy_ = false;
    bool pending_ = false;
};
