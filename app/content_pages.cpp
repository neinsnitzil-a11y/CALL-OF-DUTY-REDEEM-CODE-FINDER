#include "content_pages.hpp"

#include <algorithm>

#include <QApplication>
#include <QFont>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "../core/ai.hpp"
#include "../core/games.hpp"
#include "../core/patches.hpp"
#include "../core/community.hpp"
#include "../core/studio.hpp"
#include "../core/util.hpp"
#include "aiworker.hpp"
#include "theme.hpp"
#include "widgets.hpp"

using namespace hunter;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& x) { return x.toStdString(); }

QVBoxLayout* pageLayout(QWidget* w) {
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(28, 22, 28, 18);
    v->setSpacing(12);
    return v;
}
QTableWidget* table(const QStringList& headers) {
    auto* t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->setShowGrid(false);
    t->verticalHeader()->setDefaultSectionSize(32);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}
QString gameOf(const QComboBox* c) { return c->currentData().toString(); }
QString esc(const std::string& x) { return q(x).toHtmlEscaped(); }
}  // namespace

QComboBox* gameCombo(bool currentOnly) {
    auto* c = new QComboBox;
    for (const auto& id : currentTitles()) c->addItem(q(gameById(id).name), q(id));
    if (!currentOnly)
        for (const auto& g : allGames())
            if (g.id != "other" && std::find(currentTitles().begin(), currentTitles().end(), g.id) == currentTitles().end())
                c->addItem(q(g.name), q(g.id));
    c->setCurrentIndex(c->findData("warzone"));
    return c;
}

// ════════════════════════════════ AI helper ════════════════════════════════
AiPage::AiPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    auto* right = new QWidget;
    auto* rh = new QHBoxLayout(right);
    rh->setContentsMargins(0, 0, 0, 0);
    pill_ = new QLabel;
    pill_->setObjectName("Pill");
    auto* setupBtn = button("⚙ AI setup", "ghost");
    rh->addWidget(pill_);
    rh->addWidget(setupBtn);
    v->addWidget(pageHeader("AI helper", "Ask anything — builds, settings, codes, or any other question. Answers type out live, "
                                          "with sources and build screenshots when it looks things up.", right));

    // provider setup (saved in your private .env)
    setup_ = new QGroupBox("AI provider — any AI API works");
    auto* sf = new QFormLayout(setup_);
    provider_ = new QComboBox;
    for (const auto& p : aiPresets()) provider_->addItem(q(p.name));
    key_ = new QLineEdit;
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText("your API key (not needed for local servers)");
    model_ = new QComboBox;
    model_->setEditable(true);
    model_->setMinimumWidth(280);
    base_ = new QLineEdit;
    base_->setPlaceholderText("server address — only for custom / self-hosted servers");
    auto* row = new QHBoxLayout;
    auto* list = button("List models", "ghost");
    auto* test = button("Test connection", "ghost");
    auto* save = button("Save", "accent");
    row->addWidget(list);
    row->addWidget(test);
    row->addStretch(1);
    row->addWidget(save);
    setupResult_ = label("", "Muted");
    sf->addRow("Provider", provider_);
    sf->addRow("API key", key_);
    sf->addRow("Model", model_);
    sf->addRow("Address", base_);
    sf->addRow(row);
    sf->addRow(setupResult_);
    v->addWidget(setup_);
    connect(setupBtn, &QPushButton::clicked, this, [this] { setup_->setVisible(!setup_->isVisible()); });
    connect(provider_, &QComboBox::currentIndexChanged, this, [this] {
        bool custom = provider_->currentText().startsWith("Custom");
        base_->setEnabled(custom);
        if (!custom) base_->clear();
    });
    connect(list, &QPushButton::clicked, this, [this] {
        setupResult_->setText("Asking the provider for its models…");
        emit ctx_.ai->listModels(provider_->currentText(), key_->text().trimmed(), base_->text().trimmed());
    });
    connect(test, &QPushButton::clicked, this, [this] {
        setupResult_->setText("Testing…");
        emit ctx_.ai->testAi(provider_->currentText(), key_->text().trimmed(), model_->currentText().trimmed(), base_->text().trimmed());
    });
    connect(save, &QPushButton::clicked, this, &AiPage::saveSetup);
    connect(ctx.ai->worker(), &AiWorker::models, this, [this](const QStringList& models, const QString& suggested, const QString& err) {
        if (!err.isEmpty()) { setupResult_->setText("✖ " + err); return; }
        QString keep = model_->currentText();
        model_->clear();
        model_->addItems(models);
        model_->setCurrentText(keep.isEmpty() ? suggested : keep);
        setupResult_->setText(QStringLiteral("%1 models. Suggested: %2").arg(models.size()).arg(suggested));
    });
    connect(ctx.ai->worker(), &AiWorker::testResult, this, [this](bool ok, const QString& msg) {
        setupResult_->setText(msg);
        if (ok) saveSetup();
    });

    // chat
    chat_ = new QTextBrowser;
    chat_->setOpenExternalLinks(true);
    chat_->setPlaceholderText("Ask something below — e.g. “best Warzone loadout right now?” or anything else.");
    v->addWidget(chat_, 1);
    shots_ = new QListWidget;
    shots_->setViewMode(QListView::IconMode);
    shots_->setIconSize(QSize(200, 112));
    shots_->setFixedHeight(150);
    shots_->setFlow(QListView::LeftToRight);
    shots_->setWrapping(false);
    shots_->setVisible(false);
    connect(shots_, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        QString page = it->data(Qt::UserRole + 1).toString();
        QDesktopServices::openUrl(page.startsWith("https://") ? QUrl(page) : QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
    });
    v->addWidget(shots_);
    status_ = label("", "Muted");
    v->addWidget(status_);

    auto* inputRow = new QHBoxLayout;
    input_ = new QPlainTextEdit;
    input_->setPlaceholderText("Ask anything…   (Ctrl+Enter to send)");
    input_->setFixedHeight(70);
    inputRow->addWidget(input_, 1);
    auto* buttons = new QVBoxLayout;
    sendBtn_ = button("Send ➤", "accent");
    stopBtn_ = button("■ Stop", "ghost");
    stopBtn_->setEnabled(false);
    buttons->addWidget(sendBtn_);
    buttons->addWidget(stopBtn_);
    inputRow->addLayout(buttons);
    v->addLayout(inputRow);
    auto* opts = new QHBoxLayout;
    game_ = gameCombo();
    web_ = new QCheckBox("🌐 Look it up on the web");
    web_->setChecked(true);
    auto* attach = button("📎 Attach image", "ghost");
    attachLbl_ = label("", "Muted");
    auto* fresh = button("🆕 New chat", "ghost");
    saveBtn_ = button("💾 Save answer", "ghost");
    discordBtn_ = button("📣 To Discord", "ghost");
    saveBtn_->setEnabled(false);
    discordBtn_->setEnabled(false);
    opts->addWidget(label("Game", "Muted"));
    opts->addWidget(game_);
    opts->addWidget(web_);
    opts->addWidget(attach);
    opts->addWidget(attachLbl_);
    opts->addStretch(1);
    opts->addWidget(fresh);
    opts->addWidget(saveBtn_);
    opts->addWidget(discordBtn_);
    v->addLayout(opts);

    connect(sendBtn_, &QPushButton::clicked, this, &AiPage::send);
    auto* sendKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), input_);
    sendKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sendKey, &QShortcut::activated, this, &AiPage::send);
    connect(stopBtn_, &QPushButton::clicked, this, [this] { ctx_.ai->worker()->stop = true; status_->setText("Stopping…"); });
    connect(attach, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Attach an image", QString(), "Images (*.png *.jpg *.jpeg *.webp *.gif)");
        if (f.isEmpty()) return;
        attachments_ = {f};
        attachLbl_->setText("📎 " + QFileInfo(f).fileName() + "  (sent with your next question)");
    });
    connect(fresh, &QPushButton::clicked, this, &AiPage::newChat);
    connect(saveBtn_, &QPushButton::clicked, this, [this] {
        Json e = Json::object();
        e["saved_at"] = utcNowIso();
        e["game"] = s(gameOf(game_));
        e["question"] = s(lastQuestion_);
        e["answer"] = s(lastAnswer_);
        e["sources"] = lastResult_.get("sources").isArray() ? lastResult_.get("sources") : Json::array();
        fs::path p = ctx_.engine.config.paths().dataDir / "data" / "ai_answers.jsonl";
        fs::create_directories(p.parent_path());
        std::string existing = readFile(p);
        writeFileAtomic(p, existing + e.dump() + "\n");
        Toast::popup(ctx_.window, "Answer saved.");
    });
    connect(discordBtn_, &QPushButton::clicked, this, [this] {
        QString text = "**" + lastQuestion_ + "**\n" + lastAnswer_;
        emit toStudio("custom", gameOf(game_), text.left(1900));
    });

    // live events from the worker
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(60);
    connect(&renderTimer_, &QTimer::timeout, this, &AiPage::render);
    connect(ctx.ai->worker(), &AiWorker::event, this, [this](int id, const QString& kind, const QString& data) {
        if (id != requestId_) return;
        Json d;
        try { d = Json::parse(s(data)); } catch (...) { return; }
        if (kind == "text") live_ += q(d.toString());
        else if (kind == "line") liveLines_ += "_" + q(d.toString()) + "_  \n";
        else if (kind == "status") status_->setText(q(d.toString()));
        else if (kind == "image") {
            auto* it = new QListWidgetItem(QIcon(q(jstr(d, "file"))), q(jstr(d, "weapon", "screenshot")) +
                                                                          (jstr(d, "site").empty() ? "" : " · " + q(jstr(d, "site"))));
            it->setToolTip(jstr(d, "source_page").empty() ? "Your screenshot" : "Credit: " + q(jstr(d, "site")) + "\n" + q(jstr(d, "source_page")));
            it->setData(Qt::UserRole, q(jstr(d, "file")));
            it->setData(Qt::UserRole + 1, q(jstr(d, "source_page")));
            shots_->addItem(it);
            shots_->setVisible(true);
        }
        if (!renderTimer_.isActive()) renderTimer_.start();
    });
    connect(ctx.ai->worker(), &AiWorker::askDone, this, [this](int id, const QString& result, const QString& error) {
        if (id != requestId_) return;
        ctx_.ai->setBusy(false);
        sendBtn_->setEnabled(true);
        stopBtn_->setEnabled(false);
        status_->clear();
        if (!error.isEmpty()) {
            md_ += "\n\n" + liveLines_ + (live_.isEmpty() ? "" : live_ + "\n\n") +
                   (error == "stopped" ? QStringLiteral("⏹ _stopped_") : "✖ **" + error + "**") + "\n\n---\n";
            live_.clear();
            liveLines_.clear();
            render();
            return;
        }
        Json r = Json::parse(s(result));
        lastResult_ = r;
        lastAnswer_ = q(jstr(r, "answer"));
        QString sources;
        int i = 1;
        for (const auto& u : r.get("sources").elements()) {
            std::string title = jstr(r.get("source_titles"), u.toString());
            sources += QStringLiteral("[%1] [%2](%3)  \n").arg(i++).arg(q(title.empty() ? u.toString() : title).left(90), q(u.toString()));
            if (i > 12) break;
        }
        md_ += "\n\n" + lastAnswer_ + "\n\n" + (sources.isEmpty() ? QString() : "**Sources**  \n" + sources) +
               "_" + q(jstr(r, "search")) + "_\n\n---\n";
        live_.clear();
        liveLines_.clear();
        render();
        Json turn = Json::object();
        turn["q"] = s(lastQuestion_);
        turn["a"] = s(lastAnswer_);
        turn["game"] = s(gameOf(game_));
        turn["sources"] = r.get("sources");
        turns_.push(turn);
        saveTurns();
        saveBtn_->setEnabled(true);
        discordBtn_->setEnabled(true);
    });

    try { turns_ = Json::parse(readFile(ctx.engine.config.paths().dataDir / "data" / "chat_history.json")); } catch (...) {}
    if (!turns_.isArray()) turns_ = Json::array();
    const auto& all = turns_.elements();
    if (!all.empty()) {
        md_ = QStringLiteral("_Earlier in this chat (%1 message(s)) — the helper remembers it. 🆕 New chat starts over._\n\n").arg(all.size());
        for (size_t k = all.size() > 6 ? all.size() - 6 : 0; k < all.size(); ++k)
            md_ += "**You:** " + q(jstr(all[k], "q")) + "\n\n" + q(jstr(all[k], "a")) + "\n\n---\n";
    }
    loadSetup();
    render();
}

void AiPage::refresh() {
    bool keepOpen = setup_->isVisible();
    loadSetup();
    if (keepOpen) setup_->setVisible(true);
}

void AiPage::loadSetup() {
    Config& c = ctx_.engine.config;
    QString provider = q(c.env("AI_PROVIDER", "OpenAI"));
    int i = provider_->findText(provider);
    provider_->setCurrentIndex(i < 0 ? 0 : i);
    key_->setText(q(c.env("AI_API_KEY")));
    model_->setCurrentText(q(c.env("AI_MODEL")));
    base_->setText(q(c.env("AI_BASE_URL")));
    base_->setEnabled(provider_->currentText().startsWith("Custom"));
    bool ready = !c.env("AI_MODEL").empty() && (!c.env("AI_API_KEY").empty() || provider.contains("local"));
    pill_->setText(ready ? "● " + provider + " · " + q(c.env("AI_MODEL")) : QStringLiteral("○ Not set up"));
    pill_->setProperty("state", ready ? "on" : "off");
    repolish(pill_);
    setup_->setVisible(!ready);
}

void AiPage::saveSetup() {
    ctx_.engine.config.writeEnv({{"AI_PROVIDER", s(provider_->currentText())},
                                 {"AI_API_KEY", s(key_->text().trimmed())},
                                 {"AI_MODEL", s(model_->currentText().trimmed())},
                                 {"AI_BASE_URL", s(base_->text().trimmed())}});
    loadSetup();
    setup_->setVisible(true);
    Toast::popup(ctx_.window, "AI settings saved (in your private settings file).");
}

QString AiPage::historyJson() const {
    Json h = Json::array();
    const auto& all = turns_.elements();
    for (size_t k = all.size() > 5 ? all.size() - 5 : 0; k < all.size(); ++k) {
        Json u = Json::object(), a = Json::object();
        u["role"] = "user";
        u["text"] = jstr(all[k], "q");
        a["role"] = "assistant";
        a["text"] = jstr(all[k], "a");
        h.push(u);
        h.push(a);
    }
    return q(h.dump());
}

void AiPage::saveTurns() const {
    Json keep = Json::array();
    const auto& all = turns_.elements();
    for (size_t k = all.size() > 60 ? all.size() - 60 : 0; k < all.size(); ++k) keep.push(all[k]);
    writeFileAtomic(ctx_.engine.config.paths().dataDir / "data" / "chat_history.json", keep.dump());
}

void AiPage::newChat() {
    turns_ = Json::array();
    saveTurns();
    md_.clear();
    live_.clear();
    shots_->clear();
    shots_->setVisible(false);
    render();
}

void AiPage::askAbout(const QString& question, const QString& game) {
    int i = game_->findData(game);
    if (i >= 0) game_->setCurrentIndex(i);
    input_->setPlainText(question);
    send();
}

void AiPage::send() {
    QString question = input_->toPlainText().trimmed();
    if (question.isEmpty() || ctx_.ai->busy()) return;
    if (ctx_.engine.config.env("AI_MODEL").empty()) {
        setup_->setVisible(true);
        Toast::popup(ctx_.window, "Set up an AI provider first (any provider works — even a free local one).", false, 5000);
        return;
    }
    lastQuestion_ = question;
    input_->clear();
    shots_->clear();
    shots_->setVisible(false);
    md_ += "\n\n**You:** " + question + (attachments_.isEmpty() ? QString() : "  📎") + "\n";
    live_.clear();
    liveLines_.clear();
    ctx_.ai->setBusy(true);
    ctx_.ai->worker()->stop = false;
    sendBtn_->setEnabled(false);
    stopBtn_->setEnabled(true);
    saveBtn_->setEnabled(false);
    discordBtn_->setEnabled(false);
    status_->setText("thinking…");
    ++requestId_;
    emit ctx_.ai->ask(requestId_, question, gameOf(game_), web_->isChecked(), historyJson(), attachments_);
    attachments_.clear();
    attachLbl_->clear();
    render();
}

void AiPage::render() {
    QString doc = md_;
    if (!liveLines_.isEmpty() || !live_.isEmpty()) doc += "\n\n" + liveLines_ + "\n" + live_ + " ▍";
    chat_->setMarkdown(doc);
    chat_->verticalScrollBar()->setValue(chat_->verticalScrollBar()->maximum());
}

// ════════════════════════════════ Builds & settings ════════════════════════════════
BuildsPage::BuildsPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    game_ = gameCombo();
    v->addWidget(pageHeader("Builds & settings", "The current meta builds and best settings for every Call of Duty — "
                                                 "hand-picked, plus what the research finds across build sites (2+ sites = verified).",
                            game_));
    season_ = label("", "PageHint");
    v->addWidget(season_);

    auto* bar = new QHBoxLayout;
    rBuilds_ = button("🔬 Research builds", "accent");
    rSettings_ = button("🔬 Research settings");
    stop_ = button("■ Stop", "ghost");
    stop_->setEnabled(false);
    useAi_ = new QCheckBox("Use the AI too (reads pages and screenshots)");
    useAi_->setChecked(true);
    auto* update = button("⟳ Check for newer builds", "ghost");
    bar->addWidget(rBuilds_);
    bar->addWidget(rSettings_);
    bar->addWidget(stop_);
    bar->addWidget(useAi_);
    bar->addStretch(1);
    bar->addWidget(update);
    v->addLayout(bar);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setFixedHeight(110);
    log_->setVisible(false);
    v->addWidget(log_);

    auto* tabs = new QTabWidget;
    // curated builds
    auto* cur = new QSplitter;
    builds_ = new QListWidget;
    builds_->setMinimumWidth(260);
    buildDetail_ = new QTextBrowser;
    buildDetail_->setOpenExternalLinks(true);
    auto* curRight = new QWidget;
    auto* crv = new QVBoxLayout(curRight);
    crv->setContentsMargins(0, 0, 0, 0);
    crv->addWidget(buildDetail_, 1);
    auto* curBtns = new QHBoxLayout;
    auto* copy = button("📋 Copy build", "ghost");
    auto* toDiscord = button("📣 Send to Discord", "ghost");
    auto* ask = button("🤖 Ask the AI about it", "ghost");
    curBtns->addWidget(copy);
    curBtns->addWidget(toDiscord);
    curBtns->addWidget(ask);
    curBtns->addStretch(1);
    crv->addLayout(curBtns);
    cur->addWidget(builds_);
    cur->addWidget(curRight);
    cur->setStretchFactor(1, 1);
    tabs->addTab(cur, "🔫 Best builds");
    // researched
    auto* res = new QSplitter(Qt::Vertical);
    auto* resTop = new QWidget;
    auto* rtv = new QVBoxLayout(resTop);
    rtv->setContentsMargins(0, 0, 0, 0);
    researchInfo_ = label("", "Muted");
    researched_ = table({"WEAPON", "SITES", "VERIFIED", "READ FROM", "CHECK"});
    researched_->setColumnWidth(0, 200);
    researched_->setColumnWidth(1, 60);
    researched_->setColumnWidth(2, 90);
    researched_->setColumnWidth(3, 140);
    rtv->addWidget(researchInfo_);
    rtv->addWidget(researched_, 1);
    researchedDetail_ = new QTextBrowser;
    researchedDetail_->setOpenExternalLinks(true);
    res->addWidget(resTop);
    res->addWidget(researchedDetail_);
    tabs->addTab(res, "🔬 Researched builds");
    // settings
    auto* set = new QSplitter(Qt::Vertical);
    settings_ = new QTextBrowser;
    settings_->setOpenExternalLinks(true);
    researchedSettings_ = table({"SECTION", "SETTING", "VALUE", "SITES", "OTHER VALUES SEEN", "WHY"});
    researchedSettings_->setColumnWidth(0, 120);
    researchedSettings_->setColumnWidth(1, 200);
    researchedSettings_->setColumnWidth(2, 160);
    researchedSettings_->setColumnWidth(3, 60);
    researchedSettings_->setColumnWidth(4, 170);
    set->addWidget(settings_);
    set->addWidget(researchedSettings_);
    tabs->addTab(set, "⚙ Best settings");

    // your own builds
    auto* mineW = new QSplitter;
    mine_ = new QListWidget;
    mine_->setMinimumWidth(260);
    mineDetail_ = new QTextBrowser;
    auto* mineRight = new QWidget;
    auto* mrv = new QVBoxLayout(mineRight);
    mrv->setContentsMargins(0, 0, 0, 0);
    mrv->addWidget(mineDetail_, 1);
    auto* mineBtns = new QHBoxLayout;
    auto* mNew = button("＋ New build", "accent");
    auto* mEdit = button("✎ Edit", "ghost");
    auto* mDel = button("🗑 Delete", "ghost");
    auto* mCopy = button("📋 Copy", "ghost");
    auto* mExp = button("⬆ Export", "ghost");
    auto* mImp = button("⬇ Import", "ghost");
    for (QWidget* w : std::initializer_list<QWidget*>{mNew, mEdit, mDel, mCopy, mExp, mImp}) mineBtns->addWidget(w);
    mineBtns->addStretch(1);
    mrv->addLayout(mineBtns);
    mrv->addWidget(label("Your builds also show up on For you, scored against your playstyle. Share one with everyone from "
                         "Community → Share a build.", "Muted"));
    mineW->addWidget(mine_);
    mineW->addWidget(mineRight);
    mineW->setStretchFactor(1, 1);
    tabs->addTab(mineW, "⭐ My builds");
    connect(mine_, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r < 0 || r >= int(mineRows_.size())) { mineDetail_->clear(); return; }
        const Json& b = mineRows_.elements()[size_t(r)];
        QString html = "<h2>" + q(jstr(b, "weapon")).toHtmlEscaped() + "</h2><p style='color:" + theme::current().muted.name() + "'>" +
                       q(jstr(b, "class")).toHtmlEscaped() + " · " + q(jstr(b, "mode")).toHtmlEscaped() +
                       (jstr(b, "playstyle").empty() ? QString() : " · " + q(jstr(b, "playstyle")).toHtmlEscaped()) + "</p><table cellpadding='4'>";
        for (auto& a : b.get("attachments").elements())
            html += "<tr><td style='color:" + theme::current().muted.name() + "'>" + q(a.elements()[0].toString()).toHtmlEscaped() +
                    "</td><td><b>" + q(a.elements()[1].toString()).toHtmlEscaped() + "</b></td></tr>";
        html += "</table>";
        auto list = [](const Json& arr) {
            QStringList v;
            for (auto& x : arr.elements()) v << q(x.toString()).toHtmlEscaped();
            return v.join(", ");
        };
        if (b.get("perks").size()) html += "<p><b>Perks:</b> " + list(b.get("perks")) + "</p>";
        if (b.get("equipment").size()) html += "<p><b>Equipment:</b> " + list(b.get("equipment")) + "</p>";
        if (!jstr(b, "notes").empty()) html += "<p>" + q(jstr(b, "notes")).toHtmlEscaped().replace("\n", "<br>") + "</p>";
        html += "<p style='color:" + theme::current().muted.name() + "'>Updated " + q(jstr(b, "updated")).left(10) + "</p>";
        mineDetail_->setHtml(html);
    });
    auto current = [this]() -> Json {
        int r = mine_->currentRow();
        return r >= 0 && r < int(mineRows_.size()) ? mineRows_.elements()[size_t(r)] : Json();
    };
    connect(mine_, &QListWidget::itemDoubleClicked, this, [this, current] { if (current().isObject()) editMine(current()); });
    connect(mNew, &QPushButton::clicked, this, [this] {
        Json b = Json::object();
        b["game"] = s(game());
        editMine(b);
    });
    connect(mEdit, &QPushButton::clicked, this, [this, current] { if (current().isObject()) editMine(current()); });
    connect(mDel, &QPushButton::clicked, this, [this, current] {
        Json b = current();
        if (!b.isObject()) return;
        if (QMessageBox::question(this, "Delete build", "Delete your " + q(jstr(b, "weapon")) + " build?") != QMessageBox::Yes) return;
        MyBuilds(ctx_.engine.config.paths()).remove(jstr(b, "id"));
        refreshMine();
    });
    connect(mCopy, &QPushButton::clicked, this, [this, current] {
        Json b = current();
        if (!b.isObject()) return;
        QGuiApplication::clipboard()->setText(q(buildText(b, gameById(jstr(b, "game")).label())));
        Toast::popup(ctx_.window, "Build copied.");
    });
    connect(mExp, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(this, "Export your builds", "my-cod-builds.json", "Builds (*.json)");
        if (path.isEmpty()) return;
        try {
            writeFileAtomic(fs::path(path.toStdWString()), MyBuilds(ctx_.engine.config.paths()).exportJson());
            Toast::popup(ctx_.window, "Builds saved — friends can import the file under My builds.");
        } catch (const std::exception& e) { Toast::popup(ctx_.window, q(e.what()), false); }
    });
    connect(mImp, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getOpenFileName(this, "Import builds", QString(), "Builds (*.json)");
        if (path.isEmpty()) return;
        try {
            int n = MyBuilds(ctx_.engine.config.paths()).importJson(readFile(fs::path(path.toStdWString())));
            Toast::popup(ctx_.window, QStringLiteral("%1 build(s) imported.").arg(n));
        } catch (const std::exception& e) { Toast::popup(ctx_.window, q(e.what()), false); }
        refreshMine();
    });
    v->addWidget(tabs, 1);

    connect(game_, &QComboBox::currentIndexChanged, this, &BuildsPage::refresh);
    connect(builds_, &QListWidget::currentRowChanged, this, &BuildsPage::showBuild);
    connect(researched_, &QTableWidget::itemSelectionChanged, this, &BuildsPage::showResearched);
    connect(rBuilds_, &QPushButton::clicked, this, [this] { runResearch("builds"); });
    connect(rSettings_, &QPushButton::clicked, this, [this] { runResearch("settings"); });
    connect(stop_, &QPushButton::clicked, this, [this] { ctx_.ai->worker()->stop = true; log_->appendPlainText("Stopping…"); });
    connect(update, &QPushButton::clicked, this, [this] { emit ctx_.ai->checkMetaUpdate(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        int r = builds_->currentRow();
        if (r < 0) return;
        QApplication::clipboard()->setText(q(buildText(curated_.elements()[size_t(r)], gameById(s(game())).label())));
        Toast::popup(ctx_.window, "Build copied.");
    });
    connect(toDiscord, &QPushButton::clicked, this, [this] { emit toStudio("builds", game(), ""); });
    connect(ask, &QPushButton::clicked, this, [this] {
        int r = builds_->currentRow();
        if (r < 0) return;
        emit askAi(QStringLiteral("Is the %1 build in %2 still meta? What are good alternatives?")
                       .arg(q(jstr(curated_.elements()[size_t(r)], "weapon")), q(gameById(s(game())).name)),
                   game());
    });
    connect(ctx.ai->worker(), &AiWorker::researchLine, this, [this](const QString& l) { log_->appendPlainText(l); });
    connect(ctx.ai->worker(), &AiWorker::researchDone, this, [this](const QString& g, const QString& kind, const QString& err) {
        ctx_.ai->setBusy(false);
        rBuilds_->setEnabled(true);
        rSettings_->setEnabled(true);
        stop_->setEnabled(false);
        Toast::popup(ctx_.window, err.isEmpty() ? QStringLiteral("Research finished (%1).").arg(kind) : "Research: " + err, err.isEmpty());
        if (g == game()) refresh();
    });
    refresh();
}

QString BuildsPage::game() const { return gameOf(game_); }

void BuildsPage::setGame(const QString& g) {
    int i = game_->findData(g);
    if (i >= 0) game_->setCurrentIndex(i);
}

void BuildsPage::refreshMine() {
    mineRows_ = MyBuilds(ctx_.engine.config.paths()).forGame(s(game()));
    mine_->clear();
    for (const auto& b : mineRows_.elements())
        mine_->addItem(q(jstr(b, "weapon")) + "   ·   " + q(jstr(b, "class")) + (jstr(b, "mode").empty() ? QString() : "   ·   " + q(jstr(b, "mode"))));
    if (mine_->count()) mine_->setCurrentRow(0);
    else mineDetail_->setHtml("<p>No builds of your own for this game yet. Press <b>New build</b> to make one.</p>");
}

void BuildsPage::editMine(Json b) {
    QDialog d(this);
    d.setWindowTitle(jstr(b, "id").empty() ? "New build" : "Edit build");
    d.resize(560, 0);
    auto* f = new QFormLayout(&d);
    f->setContentsMargins(20, 20, 20, 20);
    auto* g = gameCombo();
    g->setCurrentIndex(std::max(0, g->findData(q(jstr(b, "game")))));
    auto* weapon = new QLineEdit(q(jstr(b, "weapon")));
    auto* cls = new QLineEdit(q(jstr(b, "class")));
    cls->setPlaceholderText("e.g. Assault rifle");
    auto* mode = new QComboBox;
    for (const char* x : {"multiplayer", "battle royale", "resurgence", "ranked", "zombies", "any"}) mode->addItem(x, x);
    mode->setCurrentIndex(std::max(0, mode->findData(q(jstr(b, "mode", "multiplayer")))));
    auto* style = new QComboBox;
    style->addItem("—", "");
    for (const auto& p : playstyles()) style->addItem(q(p.label), q(p.id));
    style->setCurrentIndex(std::max(0, style->findData(q(jstr(b, "playstyle")))));
    auto* range = new QComboBox;
    for (const char* x : {"", "close", "mid", "long"}) range->addItem(*x ? x : "—", x);
    range->setCurrentIndex(std::max(0, range->findData(q(jstr(b, "range")))));
    auto* att = new QPlainTextEdit;
    QStringList lines;
    for (auto& a : b.get("attachments").elements()) if (a.size() >= 2) lines << q(a.elements()[0].toString()) + ": " + q(a.elements()[1].toString());
    att->setPlainText(lines.join('\n'));
    att->setPlaceholderText("One per line:\nOptic: …\nMuzzle: …\nBarrel: …");
    att->setFixedHeight(140);
    auto joined = [](const Json& arr) {
        QStringList v;
        for (auto& x : arr.elements()) v << q(x.toString());
        return v.join(", ");
    };
    auto* perks = new QLineEdit(joined(b.get("perks")));
    perks->setPlaceholderText("comma separated");
    auto* equip = new QLineEdit(joined(b.get("equipment")));
    equip->setPlaceholderText("comma separated");
    auto* notes = new QPlainTextEdit(q(jstr(b, "notes")));
    notes->setFixedHeight(70);
    f->addRow("Game", g);
    f->addRow("Weapon", weapon);
    f->addRow("Class", cls);
    f->addRow("Mode", mode);
    f->addRow("Playstyle", style);
    f->addRow("Range", range);
    f->addRow("Attachments", att);
    f->addRow("Perks", perks);
    f->addRow("Equipment", equip);
    f->addRow("Notes", notes);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    b["game"] = s(g->currentData().toString());
    b["weapon"] = s(weapon->text().trimmed());
    b["class"] = s(cls->text().trimmed());
    b["mode"] = s(mode->currentData().toString());
    b["playstyle"] = s(style->currentData().toString());
    b["range"] = s(range->currentData().toString());
    Json a = Json::array();
    for (const auto& l : att->toPlainText().split('\n')) {
        int c = int(l.indexOf(':'));
        if (c <= 0) continue;
        Json pr = Json::array();
        pr.push(s(l.left(c).trimmed()));
        pr.push(s(l.mid(c + 1).trimmed()));
        a.push(pr);
    }
    b["attachments"] = a;
    b["perks"] = s(perks->text());
    b["equipment"] = s(equip->text());
    b["notes"] = s(notes->toPlainText());
    try {
        MyBuilds(ctx_.engine.config.paths()).save(b);
        Toast::popup(ctx_.window, "Build saved.");
    } catch (const std::exception& e) {
        Toast::popup(ctx_.window, q(e.what()), false);
    }
    if (jstr(b, "game") != s(game())) setGame(q(jstr(b, "game")));
    refreshMine();
}

void BuildsPage::refresh() {
    refreshMine();
    MetaData meta(ctx_.engine.config.paths());
    ResearchStore store(ctx_.engine.config.paths());
    const std::string gid = s(game());
    Json m = meta.forGame(gid);
    curated_ = m.get("builds");
    season_->setText(q(jstr(m, "season")).isEmpty() ? QStringLiteral("No hand-picked builds for this game yet — research finds them.")
                                                    : q(jstr(m, "season")) + " · " + q(jstr(m, "note")) + "  (builds file " +
                                                          q(jstr(m, "updated")) + ")");
    builds_->clear();
    for (const auto& b : curated_.elements()) {
        QString tier = jstr(b, "tier").empty() ? QString() : "[" + q(jstr(b, "tier")) + "] ";
        const Json fr = buildFreshness(ctx_.engine.store, gid, jstr(b, "weapon"), jstr(b, "as_of", jstr(m, "updated")));
        const std::string st = jstr(fr, "state");
        auto* item = new QListWidgetItem(tier + q(jstr(b, "weapon")) + "   ·   " + q(jstr(b, "class")) +
                                         (st == "outdated" ? "   ⚠ outdated" : st == "check" ? "   ❔ check" : ""));
        if (st != "current") {
            item->setToolTip(q(jstr(fr, "why")));
            item->setForeground(st == "outdated" ? theme::current().red : theme::current().yellow);
        }
        builds_->addItem(item);
    }
    if (builds_->count()) builds_->setCurrentRow(0);
    else buildDetail_->setHtml("<p>No hand-picked builds for this game. Press <b>Research builds</b>.</p>");

    Json res = store.forGame(gid);
    researchedRows_ = res.get("builds").get("weapons");
    researched_->setRowCount(int(researchedRows_.size()));
    for (int i = 0; i < int(researchedRows_.size()); ++i) {
        const Json& r = researchedRows_.elements()[size_t(i)];
        std::vector<std::string> via;
        for (auto& x : r.get("via").elements()) via.push_back(x.toString());
        auto* w = new QTableWidgetItem(q(jstr(r, "weapon")));
        researched_->setItem(i, 0, w);
        researched_->setItem(i, 1, new QTableWidgetItem(QString::number(jinteger(r, "sites"))));
        auto* ver = new QTableWidgetItem(jbool(r, "verified") ? "✔ verified" : "1 site");
        ver->setForeground(jbool(r, "verified") ? theme::current().green : theme::current().muted);
        researched_->setItem(i, 2, ver);
        researched_->setItem(i, 3, new QTableWidgetItem(q(join(via, ", "))));
        researched_->setItem(i, 4, new QTableWidgetItem(q(jstr(r, "curated_match"))));
    }
    std::string when = jstr(res.get("builds"), "updated");
    researchInfo_->setText(when.empty() ? QStringLiteral("Not researched yet — press Research builds.")
                                        : QStringLiteral("Researched %1 · %2 weapon(s)").arg(q(when).left(16).replace('T', ' ')).arg(researchedRows_.size()));
    researchedDetail_->clear();

    // settings: hand-picked sections, then the researched table
    const Json& st = m.get("settings");
    QString html = "<h3>" + esc(jstr(st, "title")) + "</h3>";
    for (const auto& sec : st.get("sections").elements()) {
        html += "<h4>" + esc(jstr(sec, "name")) + "</h4><table cellpadding='4'>";
        for (const auto& it : sec.get("items").elements()) {
            const auto& e = it.elements();
            html += "<tr><td><b>" + esc(e.size() > 0 ? e[0].toString() : "") + "</b></td><td>" + esc(e.size() > 1 ? e[1].toString() : "") +
                    "</td><td style='color:" + theme::current().muted.name() + "'>" + esc(e.size() > 2 ? e[2].toString() : "") + "</td></tr>";
        }
        html += "</table>";
    }
    std::vector<std::string> srcs;
    for (const auto& [k, u] : st.get("sources").items()) srcs.push_back("<a href='" + u.toString() + "'>" + k + "</a>");
    if (!srcs.empty()) html += "<p>Sources: " + q(join(srcs, " · ")) + "</p>";
    settings_->setHtml(html);
    const Json& rs = res.get("settings").get("settings");
    researchedSettings_->setRowCount(int(rs.size()));
    for (int i = 0; i < int(rs.size()); ++i) {
        const Json& r = rs.elements()[size_t(i)];
        std::vector<std::string> other;
        for (auto& x : r.get("other_values").elements()) other.push_back(x.toString());
        researchedSettings_->setItem(i, 0, new QTableWidgetItem(q(jstr(r, "section"))));
        researchedSettings_->setItem(i, 1, new QTableWidgetItem(q(jstr(r, "setting"))));
        researchedSettings_->setItem(i, 2, new QTableWidgetItem(q(r.get("value").toString())));
        researchedSettings_->setItem(i, 3, new QTableWidgetItem(QString::number(jinteger(r, "sites"))));
        researchedSettings_->setItem(i, 4, new QTableWidgetItem(q(join(other, ", "))));
        researchedSettings_->setItem(i, 5, new QTableWidgetItem(q(jstr(r, "why"))));
    }
}

void BuildsPage::showBuild() {
    int r = builds_->currentRow();
    if (r < 0 || r >= int(curated_.size())) return;
    const Json& b = curated_.elements()[size_t(r)];
    QString html = "<h2>" + esc(jstr(b, "weapon")) + "</h2><p>" + esc(jstr(b, "class")) +
                   (jstr(b, "role").empty() ? QString() : " · " + esc(jstr(b, "role"))) +
                   (jstr(b, "tier").empty() ? QString() : " · <b>" + esc(jstr(b, "tier")) + " tier</b>") + "</p><table cellpadding='4'>";
    for (const auto& a : b.get("attachments").elements())
        html += "<tr><td style='color:" + theme::current().muted.name() + "'>" + esc(a.elements()[0].toString()) + "</td><td><b>" +
                esc(a.elements()[1].toString()) + "</b></td></tr>";
    html += "</table>";
    {
        MetaData meta(ctx_.engine.config.paths());
        const Json m = meta.forGame(s(game()));
        const Json fr = buildFreshness(ctx_.engine.store, s(game()), jstr(b, "weapon"), jstr(b, "as_of", jstr(m, "updated")));
        if (jstr(fr, "state") != "current")
            html += "<p style='color:" + (jstr(fr, "state") == "outdated" ? theme::current().red : theme::current().yellow).name() + "'>⚠ " +
                    esc(jstr(fr, "why")) + "</p>";
    }
    auto list = [](const Json& arr) {
        std::vector<std::string> v;
        for (auto& x : arr.elements()) v.push_back(x.toString());
        return q(join(v, ", ")).toHtmlEscaped();
    };
    if (b.get("perks").size()) html += "<p><b>Perks:</b> " + list(b.get("perks")) + "</p>";
    if (b.get("equipment").size()) html += "<p><b>Equipment:</b> " + list(b.get("equipment")) + "</p>";
    if (!jstr(b, "secondary").empty()) html += "<p><b>Secondary:</b> " + esc(jstr(b, "secondary")) + "</p>";
    if (!jstr(b, "source").empty())
        html += "<p style='color:" + theme::current().muted.name() + "'>Source: <a href='" + q(jstr(b, "source_url")) + "'>" +
                esc(jstr(b, "source")) + "</a> (" + esc(jstr(b, "as_of")) + ")</p>";
    ScreenshotLibrary lib(ctx_.engine.config.paths());
    auto shots = lib.listing(s(game()), jstr(b, "weapon"));
    for (size_t i = 0; i < shots.size() && i < 2; ++i)
        html += "<p><img width='420' src='" + QUrl::fromLocalFile(q(jstr(shots[i], "file"))).toString() + "'><br><small>" +
                (jstr(shots[i], "site").empty() ? QStringLiteral("Your screenshot") : "Credit: " + esc(jstr(shots[i], "site"))) + "</small></p>";
    buildDetail_->setHtml(html);
}

void BuildsPage::showResearched() {
    auto rows = researched_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    const Json& r = researchedRows_.elements()[size_t(rows.first().row())];
    QString html = "<pre style='font-family:" + theme::monoFont() + "'>" + q(researchText(r, gameById(s(game())).label())).toHtmlEscaped() + "</pre>";
    for (const auto& src : r.get("sources").elements())
        html += "<p><a href='" + q(jstr(src, "url")) + "'>" + esc(jstr(src, "url")) + "</a> · " + esc(jstr(src, "via")) + "</p>";
    researchedDetail_->setHtml(html);
}

void BuildsPage::runResearch(const QString& kind) {
    if (ctx_.ai->busy()) {
        Toast::popup(ctx_.window, "The AI helper is busy — try again in a moment.", false);
        return;
    }
    ctx_.ai->setBusy(true);
    ctx_.ai->worker()->stop = false;
    log_->clear();
    log_->setVisible(true);
    log_->appendPlainText(QStringLiteral("Researching %1 for %2 — reading build sites politely (robots.txt respected)…")
                              .arg(kind, q(gameById(s(game())).name)));
    rBuilds_->setEnabled(false);
    rSettings_->setEnabled(false);
    stop_->setEnabled(true);
    emit ctx_.ai->research(game(), kind, useAi_->isChecked());
}

// ════════════════════════════════ Screenshots ════════════════════════════════
ScreenshotsPage::ScreenshotsPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    game_ = gameCombo();
    v->addWidget(pageHeader("Screenshots", "Build and settings screenshots found by research (always with credit to the site) "
                                           "and ones you add yourself. They stay on this PC.",
                            game_));
    auto* bar = new QHBoxLayout;
    weapon_ = new QLineEdit;
    weapon_->setPlaceholderText("Filter by weapon…");
    weapon_->setClearButtonEnabled(true);
    weapon_->setFixedWidth(220);
    auto* add = button("＋ Add my screenshot", "accent");
    auto* setW = button("Set weapon", "ghost");
    auto* open = button("Open source page", "ghost");
    auto* del = button("Delete", "ghost");
    auto* discord = button("📣 Send to Discord", "ghost");
    bar->addWidget(weapon_);
    bar->addStretch(1);
    for (QWidget* w : std::initializer_list<QWidget*>{add, setW, open, del, discord}) bar->addWidget(w);
    v->addLayout(bar);
    grid_ = new QListWidget;
    grid_->setViewMode(QListView::IconMode);
    grid_->setIconSize(QSize(260, 146));
    grid_->setGridSize(QSize(290, 200));
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grid_->setWordWrap(true);
    v->addWidget(grid_, 1);
    info_ = label("", "Muted");
    v->addWidget(info_);

    connect(game_, &QComboBox::currentIndexChanged, this, &ScreenshotsPage::refresh);
    connect(weapon_, &QLineEdit::textChanged, this, &ScreenshotsPage::refresh);
    connect(grid_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        Json m = selected();
        if (m.isObject()) QDesktopServices::openUrl(QUrl::fromLocalFile(q(jstr(m, "file"))));
    });
    connect(add, &QPushButton::clicked, this, [this] {
        QStringList files = QFileDialog::getOpenFileNames(this, "Add your screenshots", QString(), "Images (*.png *.jpg *.jpeg *.webp *.gif)");
        if (files.isEmpty()) return;
        QString weapon = QInputDialog::getText(this, "Weapon", "Which weapon is this build for? (optional)");
        ScreenshotLibrary lib(ctx_.engine.config.paths());
        int ok = 0;
        for (const auto& f : files) {
            try { lib.addFile(s(gameOf(game_)), fs::path(f.toStdWString()), s(weapon.trimmed())); ++ok; }
            catch (const std::exception& e) { Toast::popup(ctx_.window, q(e.what()), false); }
        }
        if (ok) Toast::popup(ctx_.window, QStringLiteral("Added %1 screenshot(s).").arg(ok));
        refresh();
    });
    connect(setW, &QPushButton::clicked, this, [this] {
        Json m = selected();
        if (!m.isObject()) return;
        bool ok = false;
        QString w = QInputDialog::getText(this, "Weapon", "Weapon shown in this screenshot:", QLineEdit::Normal, q(jstr(m, "weapon")), &ok);
        if (!ok) return;
        ScreenshotLibrary(ctx_.engine.config.paths()).setWeapon(m, s(w.trimmed()));
        refresh();
    });
    connect(open, &QPushButton::clicked, this, [this] {
        Json m = selected();
        if (m.isObject() && startsWith(jstr(m, "source_page"), "https://")) QDesktopServices::openUrl(QUrl(q(jstr(m, "source_page"))));
    });
    connect(del, &QPushButton::clicked, this, [this] {
        auto sel = grid_->selectedItems();
        if (sel.isEmpty() || QMessageBox::question(this, "Delete", QStringLiteral("Delete %1 screenshot(s)?").arg(sel.size())) != QMessageBox::Yes)
            return;
        ScreenshotLibrary lib(ctx_.engine.config.paths());
        for (auto* it : sel) lib.remove(items_[size_t(it->data(Qt::UserRole).toInt())]);
        refresh();
    });
    connect(discord, &QPushButton::clicked, this, [this] {
        QStringList files;
        for (auto* it : grid_->selectedItems()) files << q(jstr(items_[size_t(it->data(Qt::UserRole).toInt())], "file"));
        if (files.isEmpty()) { Toast::popup(ctx_.window, "Select screenshots first.", false); return; }
        emit toStudioShots(gameOf(game_), files);
    });
}

Json ScreenshotsPage::selected() const {
    auto sel = grid_->selectedItems();
    if (sel.isEmpty()) return Json();
    return items_[size_t(sel.first()->data(Qt::UserRole).toInt())];
}

void ScreenshotsPage::refresh() {
    ScreenshotLibrary lib(ctx_.engine.config.paths());
    items_ = lib.listing(s(gameOf(game_)));
    QString filter = weapon_->text().trimmed();
    if (!filter.isEmpty())
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [&](const Json& m) { return !q(jstr(m, "weapon")).contains(filter, Qt::CaseInsensitive); }),
                     items_.end());
    grid_->clear();
    for (size_t i = 0; i < items_.size(); ++i) {
        const Json& m = items_[i];
        QPixmap px;
        QImageReader reader(q(jstr(m, "file")));
        if (reader.size().isValid()) reader.setScaledSize(reader.size().scaled(QSize(260, 146), Qt::KeepAspectRatio));
        px = QPixmap::fromImage(reader.read());
        bool own = jstr(m, "added_by") != "research";
        auto* it = new QListWidgetItem(QIcon(px), (jstr(m, "weapon").empty() ? QStringLiteral("(no weapon)") : q(jstr(m, "weapon"))) +
                                                      "\n" + (own ? QStringLiteral("your screenshot") : "credit: " + q(jstr(m, "site"))));
        it->setToolTip(own ? q(jstr(m, "note")) : q(jstr(m, "source_page")) + "\n" + q(jstr(m, "alt")));
        it->setData(Qt::UserRole, int(i));
        grid_->addItem(it);
    }
    info_->setText(QStringLiteral("%1 screenshot(s) · library %2 MB · %3")
                       .arg(items_.size())
                       .arg(lib.sizeMb(), 0, 'f', 1)
                       .arg(q(lib.root().string())));
}

// ════════════════════════════════ Discord studio ════════════════════════════════
StudioPage::StudioPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    v->addWidget(pageHeader("Discord studio", "Pick exactly what to post — codes, builds, settings, screenshots or your own "
                                              "message — preview it, choose the channel and send."));
    auto* split = new QSplitter;
    auto* left = new QWidget;
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    auto* pick = new QHBoxLayout;
    kind_ = new QComboBox;
    const std::pair<const char*, const char*> kinds[] = {{"codes", "🎟 Working codes"}, {"queue", "🆕 Not posted yet"},
                                                         {"builds", "🔫 Best builds"},   {"research", "🔬 Researched builds"},
                                                         {"settings", "⚙ Best settings"}, {"shots", "🖼 Screenshots"},
                                                         {"custom", "✍ Just a message"}};
    for (auto& [key, text] : kinds) kind_->addItem(text, key);
    game_ = gameCombo();
    pick->addWidget(kind_);
    pick->addWidget(game_);
    lv->addLayout(pick);
    items_ = new QListWidget;
    lv->addWidget(items_, 1);
    auto* selRow = new QHBoxLayout;
    auto* all = button("Select all", "ghost");
    auto* none = button("None", "ghost");
    selRow->addWidget(all);
    selRow->addWidget(none);
    selRow->addStretch(1);
    lv->addLayout(selRow);
    lv->addWidget(label("Message (optional — goes above the cards)", "Muted"));
    content_ = new QPlainTextEdit;
    content_->setFixedHeight(90);
    lv->addWidget(content_);
    auto* destRow = new QFormLayout;
    dest_ = new QComboBox;
    dest_->addItem("This game's code channel", "codes");
    dest_->addItem("Updates channel", "updates");
    dest_->addItem("App-alerts channel", "system");
    dest_->addItem("Shared channel", "shared");
    dest_->addItem("Another webhook…", "custom");
    customUrl_ = new QLineEdit;
    customUrl_->setPlaceholderText("https://discord.com/api/webhooks/…");
    customUrl_->setEchoMode(QLineEdit::Password);
    customUrl_->setVisible(false);
    mention_ = new QCheckBox("Use the mention from Discord settings");
    destRow->addRow("Send to", dest_);
    destRow->addRow("", customUrl_);
    destRow->addRow("", mention_);
    lv->addLayout(destRow);
    auto* actions = new QHBoxLayout;
    auto* prev = button("👁 Preview", "ghost");
    auto* send = button("Send to Discord ➤", "accent");
    actions->addStretch(1);
    actions->addWidget(prev);
    actions->addWidget(send);
    lv->addLayout(actions);
    preview_ = new QPlainTextEdit;
    preview_->setReadOnly(true);
    preview_->setPlaceholderText("The preview shows exactly what Discord will receive.");
    preview_->setFont(QFont(theme::monoFont()));
    split->addWidget(left);
    split->addWidget(preview_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    connect(kind_, &QComboBox::currentIndexChanged, this, [this] { onlyFiles_.clear(); reloadItems(); });
    connect(game_, &QComboBox::currentIndexChanged, this, [this] { onlyFiles_.clear(); reloadItems(); });
    connect(all, &QPushButton::clicked, this, [this] { for (int i = 0; i < items_->count(); ++i) items_->item(i)->setCheckState(Qt::Checked); preview(); });
    connect(none, &QPushButton::clicked, this, [this] { for (int i = 0; i < items_->count(); ++i) items_->item(i)->setCheckState(Qt::Unchecked); preview(); });
    connect(items_, &QListWidget::itemChanged, this, [this] { preview(); });
    connect(content_, &QPlainTextEdit::textChanged, this, [this] { preview(); });
    connect(dest_, &QComboBox::currentIndexChanged, this, [this] { customUrl_->setVisible(dest_->currentData().toString() == "custom"); });
    connect(prev, &QPushButton::clicked, this, &StudioPage::preview);
    connect(send, &QPushButton::clicked, this, &StudioPage::send);
    connect(ctx.ai->worker(), &AiWorker::message, this, [this](const QString& text, bool ok) {
        if (isVisible()) Toast::popup(ctx_.window, text, ok, 5000);
    });
}

void StudioPage::refresh() { reloadItems(); }

void StudioPage::preset(const QString& kind, const QString& game, const QString& content, const QStringList& files) {
    QSignalBlocker b1(kind_), b2(game_);
    kind_->setCurrentIndex(std::max(0, kind_->findData(kind)));
    game_->setCurrentIndex(std::max(0, game_->findData(game)));
    content_->setPlainText(content);
    onlyFiles_ = files;
    reloadItems();
}

void StudioPage::reloadItems() {
    QSignalBlocker block(items_);
    items_->clear();
    objs_.clear();
    const QString kind = kind_->currentData().toString();
    const std::string gid = s(gameOf(game_));
    Paths paths = ctx_.engine.config.paths();
    auto add = [&](const QString& text, const Json& obj, bool checked) {
        auto* it = new QListWidgetItem(text);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        it->setData(Qt::UserRole, int(objs_.size()));
        items_->addItem(it);
        objs_.push_back(obj);
    };
    const Json meta = MetaData(paths).forGame(gid);
    const Json researched = ResearchStore(paths).forGame(gid);
    if (kind == "codes" || kind == "queue") {
        for (const auto& r : ctx_.engine.store.codesView(kind == "codes" ? "ready" : "all", "", gid)) {
            if (kind == "queue" && (!r.discordState.empty() || (r.status != "READY" && r.status != "VALIDATED"))) continue;
            add(q(r.code) + " · " + q(gameById(r.game).label()) + " · " + q(r.status).toLower(), Json(r.id), true);
        }
    } else if (kind == "builds") {
        for (const auto& b : meta.get("builds").elements())
            add(q(jstr(b, "weapon")) + " · " + q(jstr(b, "role")), b, false);
    } else if (kind == "research") {
        for (const auto& r : researched.get("builds").get("weapons").elements())
            add(q(jstr(r, "weapon")) + QStringLiteral(" · %1 site(s)").arg(jinteger(r, "sites")) + (jbool(r, "verified") ? " ✔" : ""), r,
                jbool(r, "verified"));
    } else if (kind == "settings") {
        for (const auto& sec : meta.get("settings").get("sections").elements())
            add(q(jstr(sec, "name")), sec, true);
    } else if (kind == "shots") {
        for (const auto& m : ScreenshotLibrary(paths).listing(gid)) {
            bool pre = onlyFiles_.isEmpty() || onlyFiles_.contains(q(jstr(m, "file")));
            add((jstr(m, "weapon").empty() ? QStringLiteral("Unknown weapon") : q(jstr(m, "weapon"))) + " · " +
                    (jstr(m, "added_by") != "research" ? QStringLiteral("your screenshot") : q(jstr(m, "site"))),
                m, pre && !onlyFiles_.isEmpty());
        }
    }
    items_->setVisible(kind != "custom");
    preview();
}

Json StudioPage::chosen(std::vector<long long>* codeIds) const {
    Json out = Json::array();
    for (int i = 0; i < items_->count(); ++i) {
        if (items_->item(i)->checkState() != Qt::Checked) continue;
        const Json& o = objs_[size_t(items_->item(i)->data(Qt::UserRole).toInt())];
        out.push(o);
        if (codeIds && o.isNumber()) codeIds->push_back(o.toInt());
    }
    return out;
}

void StudioPage::preview() {
    const std::string gid = s(gameOf(game_));
    std::string title = jstr(MetaData(ctx_.engine.config.paths()).forGame(gid).get("settings"), "title");
    auto msgs = studioMessages(ctx_.engine.config, ctx_.engine.store, s(kind_->currentData().toString()), gid, chosen(),
                               s(content_->toPlainText().trimmed()), mention_->isChecked(), title);
    preview_->setPlainText(q(studioPreview(msgs)));
}

void StudioPage::send() {
    const std::string gid = s(gameOf(game_));
    std::string url = studioDestination(ctx_.engine.config, s(dest_->currentData().toString()), gid, s(customUrl_->text()));
    if (!validWebhook(url)) {
        Toast::popup(ctx_.window, url.empty() ? QStringLiteral("No webhook for that channel yet — set it on the Discord page.")
                                              : QStringLiteral("That isn't a Discord webhook URL."),
                     false, 5000);
        return;
    }
    Json items = chosen();
    QString kind = kind_->currentData().toString();
    if (items.size() == 0 && content_->toPlainText().trimmed().isEmpty()) {
        Toast::popup(ctx_.window, "Nothing selected to send.", false);
        return;
    }
    std::string title = jstr(MetaData(ctx_.engine.config.paths()).forGame(gid).get("settings"), "title");
    Toast::popup(ctx_.window, "Sending…");
    emit ctx_.ai->studioSend(q(url), kind, q(gid), q(items.dump()), content_->toPlainText().trimmed(), mention_->isChecked(), q(title));
}
