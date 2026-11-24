#include "aiworker.hpp"

#include "../core/community.hpp"
#include "../core/i18n.hpp"
#include "../core/patches.hpp"
#include "../core/stats.hpp"
#include "../core/studio.hpp"
#include "../core/util.hpp"

using namespace hunter;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& x) { return x.toStdString(); }
}  // namespace

Helper& AiWorker::helper() {
    if (!helper_) {  // made on the worker thread, so its HTTP client lives here too
        helper_ = std::make_unique<Helper>(engine_.config, engine_.store, http_);
        helper_->renderer = renderer;
    }
    return *helper_;
}

void AiWorker::ask(int id, const QString& question, const QString& game, bool web, const QString& historyJson,
                   const QStringList& imagePaths) {
    stop = false;
    Config& c = engine_.config;
    AiClient ai(http_, c.env("AI_PROVIDER"), c.env("AI_API_KEY"), c.env("AI_MODEL"), c.env("AI_BASE_URL"));
    if (!ai.ready()) {
        emit askDone(id, "", q(ai.problem()));
        return;
    }
    ai.cancel = &stop;
    AiHistory history;
    try {
        const Json turns = Json::parse(s(historyJson));
        for (const auto& t : turns.elements())
            history.emplace_back(jstr(t, "role"), jstr(t, "text"));
    } catch (...) {}
    std::vector<AiImage> images;
    for (const auto& p : imagePaths) {
        std::string mime = ScreenshotLibrary::mimeFor(fs::path(p.toStdWString()));
        std::string bytes = readFile(fs::path(p.toStdWString()));
        if (!mime.empty() && !bytes.empty() && bytes.size() < 8000000) images.push_back({mime, bytes});
    }
    try {
        Json res = helper().ask(ai, s(question), s(game), web, history, images, [&](const std::string& kind, const Json& data) {
            if (stop) return;
            emit event(id, q(kind), q(data.dump()));
        });
        emit askDone(id, q(res.dump()), "");
    } catch (const std::exception& e) {
        emit askDone(id, "", stop ? QStringLiteral("stopped") : q(e.what()));
    }
}

void AiWorker::listModels(const QString& provider, const QString& key, const QString& base) {
    try {
        AiClient ai(http_, s(provider), s(key), "placeholder", s(base), 30, 1);
        auto list = ai.listModels();
        QStringList out;
        for (auto& m : list) out << q(m);
        emit models(out, q(suggestModel(list)), "");
    } catch (const std::exception& e) {
        emit models({}, "", q(e.what()));
    }
}

void AiWorker::testAi(const QString& provider, const QString& key, const QString& model, const QString& base) {
    AiClient ai(http_, s(provider), s(key), s(model), s(base));
    if (!ai.ready()) {
        emit testResult(false, "✖ " + q(ai.problem()));
        return;
    }
    auto [ok, msg] = ai.test();
    emit testResult(ok, q(msg));
}

void AiWorker::research(const QString& game, const QString& kind, bool useAi) {
    stop = false;
    Config& c = engine_.config;
    std::unique_ptr<AiClient> ai;
    if (useAi) ai = std::make_unique<AiClient>(http_, c.env("AI_PROVIDER"), c.env("AI_API_KEY"), c.env("AI_MODEL"), c.env("AI_BASE_URL"));
    Helper& h = helper();
    try {
        Researcher r(c.snapshot(), http_, h.meta, h.catalog, h.shots, ai && ai->ready() ? ai.get() : nullptr,
                     [this](const std::string& line) { emit researchLine(q(line)); });
        r.renderer = renderer;
        r.stop = &stop;
        if (useAi && !(ai && ai->ready())) emit researchLine("AI not set up — reading text only (set it up on the AI helper page).");
        if (kind == "settings") r.researchSettings(s(game));
        else r.researchBuilds(s(game));
        engine_.store.addAudit("RESEARCH", s(game) + " " + s(kind));
        emit researchDone(game, kind, stop ? QStringLiteral("stopped") : QString());
    } catch (const std::exception& e) {
        emit researchDone(game, kind, q(e.what()));
    }
}

void AiWorker::checkMetaUpdate() {
    std::string url = jstr(engine_.config.snapshot().get("meta"), "update_url");
    auto [changed, msg] = helper().meta.checkForUpdate(http_, url);
    emit message(q(msg), changed || msg.find("up to date") != std::string::npos);
}

void AiWorker::studioSend(const QString& url, const QString& kind, const QString& game, const QString& itemsJson,
                          const QString& content, bool mention, const QString& settingsTitle) {
    try {
        Json items = Json::parse(s(itemsJson));
        auto msgs = studioMessages(engine_.config, engine_.store, s(kind), s(game), items, s(content), mention, s(settingsTitle));
        std::vector<long long> ids;
        if (kind == "codes" || kind == "queue")
            for (auto& i : items.elements()) ids.push_back(i.toInt());
        auto [ok, msg] = hunter::studioSend(engine_.config, engine_.store, http_, s(url), msgs, s(kind), ids);
        emit message(q(msg), ok);
    } catch (const std::exception& e) {
        emit message(q(e.what()), false);
    }
}

void AiWorker::job(const QString& op, const QString& argsJson) {
    stop = false;
    Json a;
    try { a = Json::parse(s(argsJson)); } catch (...) { a = Json::object(); }
    Config& c = engine_.config;
    auto line = [this](const std::string& l) { emit researchLine(q(l)); };
    try {
        Json out = Json::object();
        if (op == "stats") {
            Helper& h = helper();
            std::unique_ptr<AiClient> ai;
            if (jbool(a, "ai")) ai = std::make_unique<AiClient>(http_, c.env("AI_PROVIDER"), c.env("AI_API_KEY"), c.env("AI_MODEL"), c.env("AI_BASE_URL"));
            Researcher r(c.snapshot(), http_, h.meta, h.catalog, h.shots, ai && ai->ready() ? ai.get() : nullptr, line);
            r.renderer = renderer;
            r.stop = &stop;
            out = r.researchStats(jstr(a, "game"), engine_.store);
        } else if (op == "patches") {
            PatchWatcher w(c, engine_.store, http_, line);
            w.renderer = renderer;
            out["new"] = w.check(jstr(a, "game"));
        } else if (op == "push_test") {
            auto [ok, msg] = pushSend(c, http_, "COD Redemption Hunter", "Push works! New codes, meta changes and builds from creators you follow will show up here.",
                                      "", "white_check_mark", 3, "codes");
            if (!ok) throw std::runtime_error(msg);
            out["message"] = msg;
        } else if (op == "translate") {
            AiClient ai(http_, c.env("AI_PROVIDER"), c.env("AI_API_KEY"), c.env("AI_MODEL"), c.env("AI_BASE_URL"));
            if (!ai.ready()) throw std::runtime_error(ai.problem());
            Translator t(c.paths());
            t.load(jstr(a, "code"));
            out["added"] = t.translateWithAi(ai, line);
        } else {
            Community com(c, http_);
            if (op == "community_refresh") out["builds"] = com.refresh(jbool(a, "force"));
            else if (op == "community_creators") { com.creatorLinks(); out["ok"] = true; }
            else if (op == "community_vote") { com.vote(jinteger(a, "number"), jbool(a, "up")); out["ok"] = true; }
            else if (op == "community_submit") out["url"] = com.submit(a.get("build"), jstr(a, "game"), jstr(a, "note"));
            else if (op == "community_results") out = com.resultsFor(jinteger(a, "number"));
            else if (op == "community_share") { com.shareResults(jinteger(a, "number"), a.get("summary")); out["ok"] = true; }
            else throw std::runtime_error("unknown job " + s(op));
        }
        emit jobDone(op, q(out.dump()), "");
    } catch (const std::exception& e) {
        emit jobDone(op, "", q(e.what()));
    }
}

AiService::AiService(Engine& engine, QObject* parent) : QObject(parent), worker_(new AiWorker(engine)) {
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &AiService::ask, worker_, &AiWorker::ask, Qt::QueuedConnection);
    connect(this, &AiService::listModels, worker_, &AiWorker::listModels, Qt::QueuedConnection);
    connect(this, &AiService::testAi, worker_, &AiWorker::testAi, Qt::QueuedConnection);
    connect(this, &AiService::research, worker_, &AiWorker::research, Qt::QueuedConnection);
    connect(this, &AiService::checkMetaUpdate, worker_, &AiWorker::checkMetaUpdate, Qt::QueuedConnection);
    connect(this, &AiService::studioSend, worker_, &AiWorker::studioSend, Qt::QueuedConnection);
    connect(this, &AiService::job, worker_, &AiWorker::job, Qt::QueuedConnection);
    thread_.setObjectName("ai");
    thread_.start();
}

AiService::~AiService() {
    worker_->stop = true;
    thread_.quit();
    thread_.wait(15000);
}
