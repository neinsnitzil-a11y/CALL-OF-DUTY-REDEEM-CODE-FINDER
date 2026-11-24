#include "redeem.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef HUNTER_WITH_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif

#include "../core/checker.hpp"
#include "../core/games.hpp"
#include "../core/util.hpp"
#include "theme.hpp"
#include "widgets.hpp"

using namespace hunter;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

// CSS selectors only (Playwright's ":has-text()" ones can't run in a page).
QJsonArray selectors(const Json& list) {
    QJsonArray out;
    for (const auto& s : list.elements())
        if (s.toString().find(":has-text") == std::string::npos) out.append(q(s.toString()));
    return out;
}
}  // namespace

RedeemWindow* RedeemWindow::open(Engine& engine, long long codeId, QWidget* parent, QString& why) {
    auto row = engine.store.getCode(codeId);
    if (!row) { why = "That code is gone from the database."; return nullptr; }
    QString url;
    bool usesUid = row->game == "codm";
    if (usesUid) {
        if (row->region == "Garena") { why = "Garena codes are redeemed inside the game (Settings → Redeem)."; return nullptr; }
        auto allowed = engine.protection.checkAllowed();
        if (!allowed.first) { why = q(allowed.second); return nullptr; }
        url = q(engine.config.redemptionUrl(row->region));
        if (url.isEmpty()) url = kOfficialCodmRedemptionUrl;
    } else {
        url = q(redeemInfo(row->game, row->code, row->kind).first);
    }
    if (!url.startsWith("https://")) { why = "No official redemption page is set for this code."; return nullptr; }
    auto* w = new RedeemWindow(engine, *row, url, parent);
    w->usesUid_ = usesUid;
    if (usesUid) {
        if (!engine.protection.limiter.acquire()) { why = "Another redemption is already open."; delete w; return nullptr; }
        w->locked_ = true;
    }
    w->attemptId_ = engine.store.startAttempt(row->id, usesUid ? engine.protection.binding.hash() : "activision", row->region);
    engine.store.addAudit("REDEMPTION_OPENED", row->code);
    w->show();
    return w;
}

RedeemWindow::RedeemWindow(Engine& engine, const CodeRow& row, const QString& url, QWidget* parent)
    : QDialog(parent), engine_(engine), row_(row), url_(url) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Redeem %1").arg(q(row.code)));
    resize(1100, 820);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    auto* banner = new QFrame;
    banner->setObjectName("Banner");
    auto* bh = new QHBoxLayout(banner);
    bh->setContentsMargins(14, 10, 14, 10);
    auto* code = label(q(row.code), "CodeBig");
    bh->addWidget(code);
    hint_ = label("", "PageHint");
    bh->addWidget(hint_, 1);
    auto* copy = button("📋 Copy code");
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(q(row_.code)); Toast::popup(this, "Code copied."); });
    bh->addWidget(copy);
    v->addWidget(banner);

#ifdef HUNTER_WITH_WEBENGINE
    view_ = new QWebEngineView(this);
    v->addWidget(view_, 1);
    connect(view_, &QWebEngineView::loadFinished, this, [this](bool ok) { if (ok) fillPage(); });
    view_->load(QUrl(url_));
    poll_ = new QTimer(this);
    poll_->setInterval(2000);
    connect(poll_, &QTimer::timeout, this, &RedeemWindow::pollResult);
    poll_->start();
    hint_->setText(usesUid_ || row.game == "codm"
                       ? "Your UID and the code are filled in. Solve the CAPTCHA and press Redeem yourself — the app never submits for you."
                       : "Sign in to your Activision account, paste the code if it isn't filled in, and pick your platform. You press Redeem.");
#else
    QApplication::clipboard()->setText(q(row.code));
    QDesktopServices::openUrl(QUrl(url_));
    hint_->setText("The code is copied and the official page is open in your browser. Paste it, solve the CAPTCHA and press "
                   "Redeem — then tell the app what the page said.");
    v->addStretch(1);
#endif

    detected_ = label("", "PageHint");
    v->addWidget(detected_);
    auto* results = new QHBoxLayout;
    results->addWidget(label("What did the page say?", "SectionTitle"));
    results->addStretch(1);
    const std::pair<const char*, const char*> outcomes[] = {
        {"✅ It worked", "SUCCESS"}, {"🎁 Already redeemed", "ALREADY_REDEEMED"}, {"⌛ Expired", "EXPIRED"},
        {"🚫 Used up", "LIMIT_REACHED"}, {"❌ Invalid", "INVALID"}, {"❔ Not sure", "UNKNOWN"}};
    for (auto& [text, outcome] : outcomes) {
        auto* b = button(text, QString(outcome) == "SUCCESS" ? "accent" : "");
        QString o = outcome;
        connect(b, &QPushButton::clicked, this, [this, o] { record(o); close(); });
        results->addWidget(b);
    }
    auto* cancel = button("Close without trying", "ghost");
    connect(cancel, &QPushButton::clicked, this, &QDialog::close);
    results->addWidget(cancel);
    v->addLayout(results);
}

void RedeemWindow::fillPage() {
#ifdef HUNTER_WITH_WEBENGINE
    Json cfg = engine_.config.snapshot();
    const Json& red = cfg.get("redemption");
    QJsonArray uidSel = usesUid_ ? selectors(red.get("selectors").get("uid")) : QJsonArray();
    QJsonArray codeSel = selectors(usesUid_ ? red.get("selectors").get("code") : red.get("account_selectors").get("code"));
    QString uid = usesUid_ ? q(engine_.protection.binding.uid()) : QString();
    // Fills inputs the way a person typing would (input + change events) so the page's own scripts notice.
    QString js = QStringLiteral(R"((function(uidSel, codeSel, uid, code){
      function fill(sels, value){
        if(!value) return false;
        for (const s of sels){ const el = document.querySelector(s);
          if (el){ const set = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
            set.call(el, value); el.dispatchEvent(new Event('input',{bubbles:true}));
            el.dispatchEvent(new Event('change',{bubbles:true})); return true; } }
        return false; }
      return JSON.stringify({uid: fill(uidSel, uid), code: fill(codeSel, code)});
    })(%1, %2, %3, %4))")
                     .arg(QString::fromUtf8(QJsonDocument(uidSel).toJson(QJsonDocument::Compact)),
                          QString::fromUtf8(QJsonDocument(codeSel).toJson(QJsonDocument::Compact)),
                          QString::fromUtf8(QJsonDocument(QJsonArray{uid}).toJson(QJsonDocument::Compact)).mid(1).chopped(1),
                          QString::fromUtf8(QJsonDocument(QJsonArray{q(row_.code)}).toJson(QJsonDocument::Compact)).mid(1).chopped(1));
    view_->page()->runJavaScript(js, [this](const QVariant& r) {
        QJsonObject o = QJsonDocument::fromJson(r.toString().toUtf8()).object();
        if (!o.value("code").toBool())
            detected_->setText("Couldn't find the code box automatically — the code is on your clipboard, paste it in.");
        QApplication::clipboard()->setText(q(row_.code));
    });
#endif
}

void RedeemWindow::pollResult() {
#ifdef HUNTER_WITH_WEBENGINE
    view_->page()->runJavaScript("document.body ? document.body.innerText.slice(0, 20000) : ''", [this](const QVariant& r) {
        const std::string text = hunter::lower(r.toString().toStdString());
        Json cfg = engine_.config.snapshot();
        for (const auto& [outcome, phrases] : cfg.get("redemption").get("result_phrases").items()) {
            for (const auto& ph : phrases.elements()) {
                if (!ph.toString().empty() && text.find(hunter::lower(ph.toString())) != std::string::npos) {
                    if (detectedOutcome_ != q(outcome)) {
                        detectedOutcome_ = q(outcome);
                        detected_->setText(QStringLiteral("The page seems to say: <b>%1</b> (\"%2\"). Confirm with a button below.")
                                               .arg(detectedOutcome_.toLower().replace('_', ' '), q(ph.toString())));
                    }
                    return;
                }
            }
        }
    });
#endif
}

void RedeemWindow::record(const QString& outcome) {
    if (done_) return;
    done_ = true;
    const std::string o = outcome.toStdString();
    engine_.store.markSubmitted(attemptId_);  // any answer counts toward the daily cap (safer for your account)
    engine_.store.finishAttempt(attemptId_, o, detectedOutcome_ == outcome ? "page text + you" : "you",
                                detectedOutcome_.toStdString());
    if (usesUid_) engine_.protection.breaker.recordOutcome(o == "SUCCESS" || o == "ALREADY_REDEEMED" ? "SUCCESS" : o);
    engine_.store.setStatus(row_.id, o);
    engine_.intel.forgetTrackRecord();
    checkOne(engine_.store, row_.id);
    engine_.store.addAudit("REDEMPTION_RESULT", row_.code + " → " + o);
    emit resultRecorded(row_.id, outcome);
}

RedeemWindow::~RedeemWindow() {
    if (locked_) engine_.protection.limiter.release();
}

void RedeemWindow::closeEvent(QCloseEvent* e) {
    if (!done_) engine_.store.finishAttempt(attemptId_, "CANCELLED", "you", "closed without a result");
    if (locked_) { engine_.protection.limiter.release(); locked_ = false; }
#ifdef HUNTER_WITH_WEBENGINE
    if (poll_) poll_->stop();
#endif
    QDialog::closeEvent(e);
}
