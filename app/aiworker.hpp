// Background thread for slow content work: AI chat, model lists, builds/settings research, builds-file
// updates and Discord studio posts. The window only ever talks to it through queued signals.
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>

#include "../core/engine.hpp"
#include "../core/helper.hpp"
#include "qthttp.hpp"

class AiWorker : public QObject {
    Q_OBJECT
public:
    explicit AiWorker(hunter::Engine& engine) : engine_(engine) {}
    std::atomic<bool> stop{false};
    std::function<std::string(const std::string&)> renderer;  // JavaScript pages (set from the GUI side)

public slots:
    void ask(int requestId, const QString& question, const QString& game, bool web, const QString& historyJson,
             const QStringList& imagePaths);
    void listModels(const QString& provider, const QString& key, const QString& base);
    void testAi(const QString& provider, const QString& key, const QString& model, const QString& base);
    void research(const QString& game, const QString& kind, bool useAi);
    void checkMetaUpdate();
    void studioSend(const QString& url, const QString& kind, const QString& game, const QString& itemsJson,
                    const QString& content, bool mention, const QString& settingsTitle);
    // Stage-4 jobs: stats, patches, community, push test, translation. args/result are JSON.
    void job(const QString& op, const QString& argsJson);

signals:
    void jobDone(const QString& op, const QString& resultJson, const QString& error);
    void event(int requestId, const QString& kind, const QString& dataJson);
    void askDone(int requestId, const QString& resultJson, const QString& error);
    void models(const QStringList& list, const QString& suggested, const QString& error);
    void testResult(bool ok, const QString& message);
    void researchLine(const QString& line);
    void researchDone(const QString& game, const QString& kind, const QString& error);
    void message(const QString& text, bool ok);

private:
    hunter::Helper& helper();
    hunter::Engine& engine_;
    QtHttpClient http_;
    std::unique_ptr<hunter::Helper> helper_;
};

// Owns the thread; lives on the GUI thread.
class AiService : public QObject {
    Q_OBJECT
public:
    explicit AiService(hunter::Engine& engine, QObject* parent = nullptr);
    ~AiService() override;
    AiWorker* worker() const { return worker_; }
    bool busy() const { return busy_; }
    void setBusy(bool b) { busy_ = b; }

signals:  // → worker (queued)
    void ask(int requestId, const QString& question, const QString& game, bool web, const QString& historyJson,
             const QStringList& imagePaths);
    void listModels(const QString& provider, const QString& key, const QString& base);
    void testAi(const QString& provider, const QString& key, const QString& model, const QString& base);
    void research(const QString& game, const QString& kind, bool useAi);
    void checkMetaUpdate();
    void studioSend(const QString& url, const QString& kind, const QString& game, const QString& itemsJson,
                    const QString& content, bool mention, const QString& settingsTitle);
    void job(const QString& op, const QString& argsJson);

private:
    QThread thread_;
    AiWorker* worker_;
    bool busy_ = false;
};
