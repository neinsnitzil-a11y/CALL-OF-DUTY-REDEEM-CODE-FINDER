#include "renderer.hpp"

#include <stdexcept>

#ifdef HUNTER_WITH_WEBENGINE
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

namespace {
// One-shot result shared between the GUI thread and the waiting scanner thread.
struct Pending {
    std::promise<std::string> promise;
    std::atomic<bool> done{false};
    void ok(std::string html) { if (!done.exchange(true)) promise.set_value(std::move(html)); }
    void fail(const std::string& why) {
        if (!done.exchange(true)) promise.set_exception(std::make_exception_ptr(std::runtime_error(why)));
    }
};

QWebEnginePage* quietPage(QWebEngineProfile* profile, QObject* parent) {
    auto* page = profile ? new QWebEnginePage(profile, parent) : new QWebEnginePage(parent);
    page->settings()->setAttribute(QWebEngineSettings::AutoLoadImages, false);  // text is all we need
    page->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    page->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);
    page->setAudioMuted(true);
    return page;
}
}  // namespace

PageRenderer::PageRenderer(const QString& userAgent, QObject* parent) : QObject(parent) {
    profile_ = new QWebEngineProfile(this);  // off the record: no cookies or cache kept on disk
    if (!userAgent.isEmpty()) profile_->setHttpUserAgent(userAgent + " (rendering)");
}

std::string PageRenderer::renderBlocking(const std::string& url, int timeoutMs) {
    if (QThread::currentThread() == thread()) throw std::runtime_error("renderBlocking called on the GUI thread");
    auto pending = std::make_shared<Pending>();
    auto future = pending->promise.get_future();
    const QString target = QString::fromStdString(url);
    QMetaObject::invokeMethod(this, [this, pending, target, timeoutMs] {
        QWebEnginePage* page = quietPage(profile_, this);
        auto* guard = new QTimer(page);
        guard->setSingleShot(true);
        QObject::connect(guard, &QTimer::timeout, page, [pending, page] {
            pending->fail("page took too long to draw");
            page->deleteLater();
        });
        guard->start(timeoutMs);
        QObject::connect(page, &QWebEnginePage::loadFinished, page, [pending, page](bool ok) {
            if (!ok) { pending->fail("page failed to load"); page->deleteLater(); return; }
            // give the page's scripts a moment to put the codes on screen
            QTimer::singleShot(2500, page, [pending, page] {
                page->toHtml([pending, page](const QString& html) {
                    pending->ok(html.toStdString());
                    page->deleteLater();
                });
            });
        });
        page->load(QUrl(target));
    }, Qt::QueuedConnection);
    if (future.wait_for(std::chrono::milliseconds(timeoutMs + 8000)) != std::future_status::ready) {
        pending->fail("timed out");
        throw std::runtime_error("JavaScript render timed out");
    }
    return future.get();
}

void checkOfficialPage(QWebEngineProfile* profile, const QString& url, const QStringList& uidSelectors,
                       const QStringList& codeSelectors, QObject* context, std::function<void(const PageCheck&)> done) {
    QWebEnginePage* page = quietPage(profile, context);
    auto finished = std::make_shared<bool>(false);
    auto finish = [page, done, finished](const PageCheck& r) {
        if (*finished) return;
        *finished = true;
        done(r);
        page->deleteLater();
    };
    QTimer::singleShot(30000, page, [finish] { PageCheck r; r.error = "The page didn't load within 30 seconds."; finish(r); });
    QObject::connect(page, &QWebEnginePage::loadFinished, page, [page, finish, uidSelectors, codeSelectors](bool ok) {
        if (!ok) { PageCheck r; r.error = "The page failed to load."; finish(r); return; }
        QTimer::singleShot(3000, page, [page, finish, uidSelectors, codeSelectors] {
            const QString js = QStringLiteral(R"((function(u, c){
                const count = sels => { let n = 0; for (const s of sels) { try { n += document.querySelectorAll(s).length; } catch(e) {} } return n; };
                const cap = document.querySelectorAll('iframe[src*="captcha"], iframe[src*="recaptcha"], iframe[src*="hcaptcha"], .g-recaptcha, .h-captcha, [class*="captcha" i]').length;
                return JSON.stringify({uid: count(u), code: count(c), captcha: cap, title: document.title});
            })(%1, %2))")
                                   .arg(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(uidSelectors)).toJson(QJsonDocument::Compact)),
                                        QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(codeSelectors)).toJson(QJsonDocument::Compact)));
            page->runJavaScript(js, [finish](const QVariant& v) {
                QJsonObject o = QJsonDocument::fromJson(v.toString().toUtf8()).object();
                PageCheck r;
                r.loaded = true;
                r.uidBoxes = o.value("uid").toInt();
                r.codeBoxes = o.value("code").toInt();
                r.captchas = o.value("captcha").toInt();
                r.title = o.value("title").toString();
                finish(r);
            });
        });
    });
    page->load(QUrl(url));
}

#else  // no WebEngine: nothing to build, the hooks stay empty
PageRenderer::PageRenderer(const QString&, QObject* parent) : QObject(parent), profile_(nullptr) {}
std::string PageRenderer::renderBlocking(const std::string&, int) { throw std::runtime_error("no renderer"); }
void checkOfficialPage(QWebEngineProfile*, const QString&, const QStringList&, const QStringList&, QObject*,
                       std::function<void(const PageCheck&)> done) {
    PageCheck r;
    r.error = "This build has no built-in browser (Qt WebEngine).";
    done(r);
}
#endif
