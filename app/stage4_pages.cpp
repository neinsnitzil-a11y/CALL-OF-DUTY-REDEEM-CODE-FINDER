#include "stage4_pages.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "../core/community.hpp"
#include "../core/games.hpp"
#include "../core/patches.hpp"
#include "../core/stats.hpp"
#include "../core/util.hpp"
#include "aiworker.hpp"
#include "content_pages.hpp"
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
    t->setSortingEnabled(false);
    t->verticalHeader()->setDefaultSectionSize(32);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}
QTableWidgetItem* cell(const QString& text, const QColor& c = QColor()) {
    auto* i = new QTableWidgetItem(text);
    if (c.isValid()) i->setForeground(c);
    return i;
}
QString numText(const Json& v, const char* unit = "") {
    if (!v.isNumber()) return "—";
    double d = v.toDouble();
    return (std::floor(d) == d ? QString::number(qlonglong(d)) : QString::number(d, 'f', 1)) + unit;
}
QString esc(const std::string& x) { return q(x).toHtmlEscaped(); }
QString gameOf(const QComboBox* c) { return c->currentData().toString(); }
}  // namespace

// ════════════════════════════════ TTK chart ════════════════════════════════
void TtkChart::setCurves(const QString& a, const std::vector<std::pair<int, int>>& ca, const QString& b,
                         const std::vector<std::pair<int, int>>& cb) {
    a_ = a; b_ = b; ca_ = ca; cb_ = cb;
    update();
}

void TtkChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = theme::current();
    QRectF area = rect().adjusted(56, 16, -16, -34);
    p.fillRect(rect(), pal.surface);
    if (ca_.empty() && cb_.empty()) {
        p.setPen(pal.muted);
        p.drawText(rect(), Qt::AlignCenter, "Pick two weapons with damage and fire-rate data to compare their time to kill.");
        return;
    }
    int maxX = 1, maxY = 1;
    for (auto* c : {&ca_, &cb_}) for (auto& [x, y] : *c) { maxX = std::max(maxX, x); maxY = std::max(maxY, y); }
    maxY = int(std::ceil(maxY * 1.15 / 100.0) * 100);
    auto pt = [&](int x, int y) { return QPointF(area.left() + area.width() * x / maxX, area.bottom() - area.height() * y / maxY); };
    p.setPen(QPen(pal.border, 1));
    for (int i = 0; i <= 4; ++i) {
        double y = area.top() + area.height() * i / 4;
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(pal.muted);
        p.drawText(QRectF(0, y - 8, 50, 16), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1 ms").arg(maxY * (4 - i) / 4));
        p.setPen(QPen(pal.border, 1));
    }
    p.setPen(pal.muted);
    for (int m = 0; m <= maxX; m += 10)
        p.drawText(QRectF(pt(m, 0).x() - 20, area.bottom() + 4, 40, 16), Qt::AlignCenter, QStringLiteral("%1 m").arg(m));
    auto draw = [&](const std::vector<std::pair<int, int>>& c, const QColor& col, const QString& name, int slot) {
        if (c.empty()) return;
        QPainterPath path(pt(c.front().first, c.front().second));
        for (size_t i = 1; i < c.size(); ++i) {  // step line: TTK only changes at damage drop-offs
            path.lineTo(pt(c[i].first, c[i - 1].second));
            path.lineTo(pt(c[i].first, c[i].second));
        }
        p.setPen(QPen(col, 2.5));
        p.drawPath(path);
        p.fillRect(QRectF(area.left() + 8 + slot * 180, area.top() + 4, 12, 12), col);
        p.setPen(pal.text);
        p.drawText(QPointF(area.left() + 26 + slot * 180, area.top() + 15), name);
    };
    draw(ca_, pal.accent, a_, 0);
    draw(cb_, pal.blue, b_, 1);
}

// ════════════════════════════════ Stats & patches ════════════════════════════════
StatsPage::StatsPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    game_ = gameCombo();
    v->addWidget(pageHeader("Weapon stats & patches", "Fire rate, damage by range, ADS and more from the stats sites, combined "
                                                      "across sites, with time to kill. Patch watch flags buffs and nerfs.",
                            game_));
    auto* bar = new QHBoxLayout;
    research_ = button("📊 Research weapon stats", "accent");
    checkPatches_ = button("🩹 Check patch notes now");
    useAi_ = new QCheckBox("Use the AI too");
    useAi_->setChecked(true);
    health_ = new QSpinBox;
    health_->setRange(50, 400);
    health_->setSuffix(" HP");
    health_->setToolTip("Total health to kill (armor included) — changes the time-to-kill numbers");
    bar->addWidget(research_);
    bar->addWidget(useAi_);
    bar->addWidget(checkPatches_);
    bar->addStretch(1);
    bar->addWidget(label("Health", "Muted"));
    bar->addWidget(health_);
    v->addLayout(bar);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setFixedHeight(90);
    log_->setVisible(false);
    v->addWidget(log_);

    auto* split = new QSplitter(Qt::Vertical);
    table_ = table({"WEAPON", "CLASS", "TTK CLOSE", "TTK FAR", "SHOTS TO KILL", "RPM", "ADS", "SPRINT→FIRE", "VELOCITY", "MAG", "SITES"});
    table_->setColumnWidth(0, 170);
    split->addWidget(table_);
    auto* bottom = new QWidget;
    auto* bh = new QHBoxLayout(bottom);
    bh->setContentsMargins(0, 0, 0, 0);
    auto* cmp = new QWidget;
    auto* cv = new QVBoxLayout(cmp);
    cv->setContentsMargins(0, 0, 0, 0);
    auto* pick = new QHBoxLayout;
    wa_ = new QComboBox;
    wb_ = new QComboBox;
    pick->addWidget(label("Compare", "SectionTitle"));
    pick->addWidget(wa_, 1);
    pick->addWidget(label("vs", "Muted"));
    pick->addWidget(wb_, 1);
    cv->addLayout(pick);
    chart_ = new TtkChart;
    cv->addWidget(chart_, 1);
    bh->addWidget(cmp, 3);
    auto* side = new QWidget;
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->addWidget(label("Patch watch", "SectionTitle"));
    patches_ = new QListWidget;
    sv->addWidget(patches_, 1);
    sv->addWidget(label("Stat changes seen on the sites", "SectionTitle"));
    changes_ = new QListWidget;
    sv->addWidget(changes_, 1);
    bh->addWidget(side, 2);
    split->addWidget(bottom);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    v->addWidget(split, 1);

    connect(game_, &QComboBox::currentIndexChanged, this, &StatsPage::refresh);
    connect(health_, &QSpinBox::valueChanged, this, [this](int hp) {
        Json cfg = ctx_.engine.config.snapshot();
        cfg["stats"]["health"][s(game())] = hp;
        ctx_.engine.config.save(cfg);
        refresh();
    });
    connect(wa_, &QComboBox::currentIndexChanged, this, &StatsPage::compare);
    connect(wb_, &QComboBox::currentIndexChanged, this, &StatsPage::compare);
    connect(patches_, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        QString url = it->data(Qt::UserRole).toString();
        if (url.startsWith("https://")) QDesktopServices::openUrl(QUrl(url));
    });
    auto start = [this](const QString& op) {
        if (ctx_.ai->busy()) { Toast::popup(ctx_.window, "Busy with another job — try again in a moment.", false); return; }
        ctx_.ai->setBusy(true);
        log_->clear();
        log_->setVisible(true);
        research_->setEnabled(false);
        checkPatches_->setEnabled(false);
        Json a = Json::object();
        a["game"] = s(game());
        a["ai"] = useAi_->isChecked();
        emit ctx_.ai->job(op, q(a.dump()));
    };
    connect(research_, &QPushButton::clicked, this, [start] { start("stats"); });
    connect(checkPatches_, &QPushButton::clicked, this, [start] { start("patches"); });
    connect(ctx.ai->worker(), &AiWorker::researchLine, this, [this](const QString& l) { if (log_->isVisible()) log_->appendPlainText(l); });
    connect(ctx.ai->worker(), &AiWorker::jobDone, this, [this](const QString& op, const QString& result, const QString& err) {
        if (op != "stats" && op != "patches") return;
        ctx_.ai->setBusy(false);
        research_->setEnabled(true);
        checkPatches_->setEnabled(true);
        if (!err.isEmpty()) { Toast::popup(ctx_.window, err, false, 5000); return; }
        Json r = Json::parse(s(result));
        Toast::popup(ctx_.window, op == "stats" ? QStringLiteral("Stats for %1 weapon(s) — %2 change(s).").arg(jinteger(r, "weapons")).arg(r.get("changes").size())
                                                : QStringLiteral("%1 new patch note(s).").arg(r.get("new").size()));
        refresh();
    });
    refresh();
}

QString StatsPage::game() const { return gameOf(game_); }

void StatsPage::refresh() {
    const std::string gid = s(game());
    Json cfg = ctx_.engine.config.snapshot();
    int hp = healthFor(cfg, gid);
    {
        QSignalBlocker b(health_);
        health_->setValue(hp);
    }
    rows_ = statsTable(ctx_.engine.store, gid, hp);
    const Palette& p = theme::current();
    table_->setRowCount(int(rows_.size()));
    QString keepA = wa_->currentText(), keepB = wb_->currentText();
    QSignalBlocker ba(wa_), bb(wb_);
    wa_->clear();
    wb_->clear();
    for (int i = 0; i < int(rows_.size()); ++i) {
        const Json& r = rows_.elements()[size_t(i)];
        QString stk = r.get("stk_close").isNumber() ? numText(r.get("stk_close")) + (r.get("stk_far").isNumber() ? "–" + numText(r.get("stk_far")) : "") : "—";
        table_->setItem(i, 0, cell(q(jstr(r, "weapon"))));
        table_->setItem(i, 1, cell(q(jstr(r, "class")), p.muted));
        table_->setItem(i, 2, cell(numText(r.get("ttk_close"), " ms"), p.green));
        table_->setItem(i, 3, cell(numText(r.get("ttk_far"), " ms")));
        table_->setItem(i, 4, cell(stk));
        table_->setItem(i, 5, cell(numText(r.get("rpm"))));
        table_->setItem(i, 6, cell(numText(r.get("ads_ms"), " ms")));
        table_->setItem(i, 7, cell(numText(r.get("sprint_to_fire_ms"), " ms")));
        table_->setItem(i, 8, cell(numText(r.get("velocity"), " m/s")));
        table_->setItem(i, 9, cell(numText(r.get("mag"))));
        std::vector<std::string> src;
        for (auto& x : r.get("sources").elements()) src.push_back(x.toString());
        table_->setItem(i, 10, cell(q(join(src, ", ")), p.muted));
        if (r.get("damage").size() && r.get("rpm").isNumber()) {
            wa_->addItem(q(jstr(r, "weapon")), i);
            wb_->addItem(q(jstr(r, "weapon")), i);
        }
    }
    if (int i = wa_->findText(keepA); i >= 0) wa_->setCurrentIndex(i);
    if (int i = wb_->findText(keepB); i >= 0) wb_->setCurrentIndex(i);
    else if (wb_->count() > 1) wb_->setCurrentIndex(1);
    compare();

    patches_->clear();
    for (const auto& pt : recentPatches(ctx_.engine.store, gid, 90).elements()) {
        QStringList ch;
        for (const auto& c : pt.get("weapon_changes").elements()) {
            std::string d = jstr(c, "direction");
            ch << QString(d == "buff" ? "⬆ " : d == "nerf" ? "⬇ " : "↔ ") + q(jstr(c, "weapon"));
        }
        std::string when = (jstr(pt, "published").empty() ? jstr(pt, "seen_at") : jstr(pt, "published")).substr(0, 10);
        auto* it = new QListWidgetItem(q(when) + "  " + q(jstr(pt, "title")).left(80) + (ch.isEmpty() ? QString() : "\n     " + ch.join("  ")));
        it->setData(Qt::UserRole, q(jstr(pt, "url")));
        it->setToolTip(q(jstr(pt, "url")));
        patches_->addItem(it);
    }
    if (!patches_->count()) patches_->addItem("No patch notes seen in the last 90 days. They're checked every few hours while scanning.");
    changes_->clear();
    for (const auto& c : ctx_.engine.store.statChanges(gid, "", 50).elements())
        changes_->addItem(q(jstr(c, "seen_at").substr(0, 10)) + "  " + q(jstr(c, "weapon")) + " · " + q(jstr(c, "field")) + ": " +
                          q(jstr(c, "old")) + " → " + q(jstr(c, "new")) + "  (" + q(jstr(c, "source")) + ")");
    if (!changes_->count()) changes_->addItem("No changes recorded yet.");
}

void StatsPage::compare() {
    int hp = health_->value();
    auto curve = [&](QComboBox* c) {
        if (c->currentIndex() < 0) return std::vector<std::pair<int, int>>{};
        return ttkCurve(rows_.elements()[size_t(c->currentData().toInt())], hp);
    };
    chart_->setCurves(wa_->currentText(), curve(wa_), wb_->currentText(), curve(wb_));
}

// ════════════════════════════════ For you ════════════════════════════════
ForYouPage::ForYouPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    game_ = gameCombo();
    v->addWidget(pageHeader("For you", "Builds picked for how you play — curated, researched and community builds, scored on "
                                       "your playstyle, tiers, votes, patches since, and your own match results.",
                            game_));
    auto* prof = new QHBoxLayout;
    input_ = new QComboBox;
    for (const char* x : {"controller", "mouse & keyboard", "touch"}) input_->addItem(x, x);
    style_ = new QComboBox;
    for (const auto& p : playstyles()) style_->addItem(q(p.label), q(p.id));
    mode_ = new QComboBox;
    for (const char* x : {"multiplayer", "battle royale", "resurgence", "ranked"}) mode_->addItem(x, x);
    prof->addWidget(label("I play on", "Muted"));
    prof->addWidget(input_);
    prof->addWidget(label("as a", "Muted"));
    prof->addWidget(style_);
    prof->addWidget(label("mostly", "Muted"));
    prof->addWidget(mode_);
    prof->addStretch(1);
    auto* log = button("＋ Log a match", "accent");
    prof->addWidget(log);
    v->addLayout(prof);
    auto* split = new QSplitter;
    recs_ = new QListWidget;
    recs_->setMinimumWidth(320);
    detail_ = new QTextBrowser;
    detail_->setOpenExternalLinks(true);
    split->addWidget(recs_);
    split->addWidget(detail_);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 2);
    v->addWidget(label("Your results by build", "SectionTitle"));
    perf_ = table({"WEAPON", "MATCHES", "K/D", "WIN RATE", "LAST PLAYED"});
    perf_->setColumnWidth(0, 200);
    perf_->setMaximumHeight(180);
    v->addWidget(perf_);

    Json p = playerProfile(ctx.engine.config.snapshot());
    input_->setCurrentIndex(std::max(0, input_->findData(q(jstr(p, "input")))));
    style_->setCurrentIndex(std::max(0, style_->findData(q(jstr(p, "playstyle")))));
    mode_->setCurrentIndex(std::max(0, mode_->findData(q(jstr(p, "mode")))));
    for (auto* c : {input_, style_, mode_}) connect(c, &QComboBox::currentIndexChanged, this, &ForYouPage::saveProfile);
    connect(game_, &QComboBox::currentIndexChanged, this, &ForYouPage::refresh);
    connect(log, &QPushButton::clicked, this, &ForYouPage::logMatch);
    connect(recs_, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r < 0 || r >= int(items_.size())) return;
        const Json& c = items_.elements()[size_t(r)];
        QString html = "<h2>" + esc(jstr(c, "weapon")) + "</h2><p>" + esc(jstr(c, "origin")) +
                       (jstr(c, "author").empty() ? QString() : " by <b>" + esc(jstr(c, "author")) + "</b>") +
                       QStringLiteral(" · fit %1</p>").arg(jnum(c, "fit"));
        const Json& fr = c.get("freshness");
        if (jstr(fr, "state") != "current")
            html += "<p style='color:" + (jstr(fr, "state") == "outdated" ? theme::current().red : theme::current().yellow).name() +
                    "'>⚠ " + esc(jstr(fr, "why")) + "</p>";
        html += "<p><b>Why it's picked</b></p><ul>";
        for (auto& r2 : c.get("reasons").elements()) html += "<li>" + esc(r2.toString()) + "</li>";
        html += "</ul><table cellpadding='4'>";
        for (auto& a : c.get("attachments").elements())
            if (a.size() >= 2)
                html += "<tr><td style='color:" + theme::current().muted.name() + "'>" + esc(a.elements()[0].toString()) + "</td><td><b>" +
                        esc(a.elements()[1].toString()) + "</b></td></tr>";
        html += "</table>";
        if (!jstr(c, "url").empty()) html += "<p><a href='" + q(jstr(c, "url")) + "'>Open it on GitHub</a></p>";
        detail_->setHtml(html);
    });
    refresh();
}

QString ForYouPage::game() const { return gameOf(game_); }

void ForYouPage::saveProfile() {
    Json cfg = ctx_.engine.config.snapshot();
    cfg["player"]["input"] = s(input_->currentData().toString());
    cfg["player"]["playstyle"] = s(style_->currentData().toString());
    cfg["player"]["mode"] = s(mode_->currentData().toString());
    ctx_.engine.config.save(cfg);
    refresh();
}

void ForYouPage::refresh() {
    const std::string gid = s(game());
    Community com(ctx_.engine.config, ctx_.engine.http);
    Json cands = buildCandidates(ctx_.engine.config, gid, com.buildsFor(gid));
    items_ = recommend(ctx_.engine.config, ctx_.engine.store, cands, gid, 12);
    recs_->clear();
    for (const auto& c : items_.elements()) {
        std::string st = jstr(c.get("freshness"), "state");
        QString badge = st == "outdated" ? " ⚠" : st == "check" ? " ❔" : "";
        recs_->addItem(QStringLiteral("%1   %2   · %3%4").arg(jnum(c, "fit"), 0, 'f', 1).arg(q(jstr(c, "weapon")), q(jstr(c, "origin")), badge));
    }
    if (recs_->count()) recs_->setCurrentRow(0);
    else detail_->setHtml("<p>No builds for this game yet — research builds or set up community builds.</p>");
    Json perf = performance(ctx_.engine.store, gid);
    std::vector<std::pair<std::string, Json>> rows;
    for (const auto& [k, v] : perf.items()) if (k != "*") rows.emplace_back(k, v);
    perf_->setRowCount(int(rows.size()));
    for (int i = 0; i < int(rows.size()); ++i) {
        const Json& r = rows[size_t(i)].second;
        perf_->setItem(i, 0, cell(q(jstr(r, "weapon"))));
        perf_->setItem(i, 1, cell(QString::number(jinteger(r, "matches"))));
        perf_->setItem(i, 2, cell(QString::number(jnum(r, "kd"), 'f', 2)));
        perf_->setItem(i, 3, cell(QStringLiteral("%1%").arg(jinteger(r, "win_rate"))));
        perf_->setItem(i, 4, cell(q(jstr(r, "last")).left(16).replace('T', ' '), theme::current().muted));
    }
}

void ForYouPage::logMatch() {
    QDialog d(this);
    d.setWindowTitle("Log a match");
    auto* f = new QFormLayout(&d);
    f->setContentsMargins(20, 20, 20, 20);
    auto* weapon = new QComboBox;
    weapon->setEditable(true);
    for (const auto& c : items_.elements()) weapon->addItem(q(jstr(c, "weapon")));
    auto* kills = new QSpinBox;
    kills->setRange(0, 300);
    auto* deaths = new QSpinBox;
    deaths->setRange(0, 300);
    auto* won = new QCheckBox("We won");
    auto* mode = new QComboBox;
    for (const char* x : {"multiplayer", "battle royale", "resurgence", "ranked"}) mode->addItem(x, x);
    mode->setCurrentIndex(mode_->currentIndex());
    auto* place = new QSpinBox;
    place->setRange(0, 150);
    place->setSpecialValueText("—");
    auto* notes = new QLineEdit;
    f->addRow("Build (weapon)", weapon);
    f->addRow("Kills", kills);
    f->addRow("Deaths", deaths);
    f->addRow("", won);
    f->addRow("Mode", mode);
    f->addRow("Placement (BR)", place);
    f->addRow("Notes", notes);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted || weapon->currentText().trimmed().isEmpty()) return;
    hunter::logMatch(ctx_.engine.store, s(game()), s(weapon->currentText().trimmed()), kills->value(), deaths->value(), won->isChecked(),
                     s(mode->currentData().toString()), place->value(), s(notes->text()));
    Toast::popup(ctx_.window, "Match logged — your picks learn from it.");
    refresh();
}

// ════════════════════════════════ Community ════════════════════════════════
CommunityPage::CommunityPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    auto* setupBtn = button("⚙ Set up", "ghost");
    v->addWidget(pageHeader("Community builds", "Builds shared by players on your community's GitHub repo — vote with 👍 / 👎, "
                                                "follow creators, submit your own. Free, no server.",
                            setupBtn));
    status_ = label("", "PageHint");
    v->addWidget(status_);
    auto* tabs = new QTabWidget;

    auto* browse = new QWidget;
    auto* bv = new QVBoxLayout(browse);
    bv->setContentsMargins(0, 8, 0, 0);
    auto* bar = new QHBoxLayout;
    game_ = gameCombo();
    game_->insertItem(0, "All games", "");
    game_->setCurrentIndex(0);
    sort_ = new QComboBox;
    sort_->addItem("Top rated", "top");
    sort_->addItem("Newest", "new");
    sort_->addItem("Most debated", "controversial");
    auto* refreshBtn = button("⟳ Refresh", "ghost");
    bar->addWidget(game_);
    bar->addWidget(sort_);
    bar->addStretch(1);
    bar->addWidget(refreshBtn);
    bv->addLayout(bar);
    auto* split = new QSplitter;
    builds_ = table({"WEAPON", "GAME", "BY", "👍", "👎", "SCORE", ""});
    builds_->setColumnWidth(0, 180);
    builds_->setColumnWidth(1, 110);
    builds_->setColumnWidth(2, 120);
    builds_->setColumnWidth(3, 45);
    builds_->setColumnWidth(4, 45);
    split->addWidget(builds_);
    auto* right = new QWidget;
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    detail_ = new QTextBrowser;
    detail_->setOpenExternalLinks(true);
    rv->addWidget(detail_, 1);
    auto* act = new QHBoxLayout;
    auto* up = button("👍", "ghost");
    auto* down = button("👎", "ghost");
    auto* follow = button("Follow creator", "ghost");
    auto* open = button("Open on GitHub", "ghost");
    auto* results = button("Everyone's results", "ghost");
    for (QWidget* w : std::initializer_list<QWidget*>{up, down, follow, open, results}) act->addWidget(w);
    act->addStretch(1);
    rv->addLayout(act);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    bv->addWidget(split, 1);
    tabs->addTab(browse, "🧰 Builds");

    creators_ = table({"CREATOR", "BUILDS", "👍", "👎", "SCORE", "GAMES", "FOLLOWING"});
    creators_->setColumnWidth(0, 180);
    tabs->addTab(creators_, "⭐ Creators");

    auto* sub = new QWidget;
    auto* sf = new QFormLayout(sub);
    sGame_ = gameCombo();
    sWeapon_ = new QLineEdit;
    sClass_ = new QLineEdit;
    sClass_->setPlaceholderText("e.g. Assault rifle");
    sMode_ = new QComboBox;
    for (const char* x : {"any", "multiplayer", "battle royale", "resurgence", "ranked", "zombies"}) sMode_->addItem(x, x);
    sStyle_ = new QComboBox;
    sStyle_->addItem("—", "");
    for (const auto& p : playstyles()) sStyle_->addItem(q(p.label), q(p.id));
    sRange_ = new QComboBox;
    for (const char* x : {"", "close", "mid", "long"}) sRange_->addItem(*x ? x : "—", x);
    sInput_ = new QComboBox;
    for (const char* x : {"", "controller", "mouse & keyboard", "touch"}) sInput_->addItem(*x ? x : "—", x);
    sAttach_ = new QPlainTextEdit;
    sAttach_->setPlaceholderText("One per line:\nOptic: Corio Eagleseye 2.5x\nMuzzle: FSS IS300\nBarrel: …");
    sAttach_->setFixedHeight(130);
    sPerks_ = new QLineEdit;
    sPerks_->setPlaceholderText("comma separated");
    sEquip_ = new QLineEdit;
    sEquip_->setPlaceholderText("comma separated");
    sSeason_ = new QLineEdit;
    sNote_ = new QPlainTextEdit;
    sNote_->setFixedHeight(70);
    sf->addRow("Game", sGame_);
    sf->addRow("Weapon", sWeapon_);
    sf->addRow("Class", sClass_);
    sf->addRow("Mode", sMode_);
    sf->addRow("Playstyle", sStyle_);
    sf->addRow("Range", sRange_);
    sf->addRow("Input", sInput_);
    sf->addRow("Attachments", sAttach_);
    sf->addRow("Perks", sPerks_);
    sf->addRow("Equipment", sEquip_);
    sf->addRow("Season / patch", sSeason_);
    sf->addRow("Notes", sNote_);
    auto* send = button("Share this build", "accent");
    sf->addRow(send);
    sf->addRow(label("With GITHUB_TOKEN set (Settings), it's posted directly; otherwise GitHub opens with everything filled in "
                     "and you press Submit.", "Muted"));
    tabs->addTab(sub, "＋ Share a build");
    v->addWidget(tabs, 1);

    connect(setupBtn, &QPushButton::clicked, this, &CommunityPage::setup);
    connect(game_, &QComboBox::currentIndexChanged, this, &CommunityPage::refresh);
    connect(sort_, &QComboBox::currentIndexChanged, this, &CommunityPage::refresh);
    connect(builds_, &QTableWidget::itemSelectionChanged, this, &CommunityPage::showBuild);
    connect(refreshBtn, &QPushButton::clicked, this, [this] {
        Json a = Json::object();
        a["force"] = true;
        status_->setText("Refreshing from GitHub…");
        emit ctx_.ai->job("community_refresh", q(a.dump()));
        emit ctx_.ai->job("community_creators", "{}");
    });
    auto voteFn = [this](bool isUp) {
        Json b = selected();
        if (!b.isObject()) return;
        if (ctx_.engine.config.env("GITHUB_TOKEN").empty()) {
            QDesktopServices::openUrl(QUrl(q(jstr(b, "url"))));
            Toast::popup(ctx_.window, "Vote with 👍 / 👎 on the GitHub page (or add GITHUB_TOKEN in Settings to vote from here).");
            return;
        }
        Json a = Json::object();
        a["number"] = jinteger(b, "number");
        a["up"] = isUp;
        emit ctx_.ai->job("community_vote", q(a.dump()));
    };
    connect(up, &QPushButton::clicked, this, [voteFn] { voteFn(true); });
    connect(down, &QPushButton::clicked, this, [voteFn] { voteFn(false); });
    connect(follow, &QPushButton::clicked, this, [this] {
        Json b = selected();
        if (b.isObject()) toggleFollow(q(jstr(b, "author")));
    });
    connect(open, &QPushButton::clicked, this, [this] {
        Json b = selected();
        if (b.isObject()) QDesktopServices::openUrl(QUrl(q(jstr(b, "url"))));
    });
    connect(results, &QPushButton::clicked, this, [this] {
        Json b = selected();
        if (!b.isObject()) return;
        Json a = Json::object();
        a["number"] = jinteger(b, "number");
        emit ctx_.ai->job("community_results", q(a.dump()));
    });
    connect(creators_, &QTableWidget::cellDoubleClicked, this, [this](int r, int col) {
        if (r < 0 || r >= int(creatorList_.size())) return;
        const Json& c = creatorList_.elements()[size_t(r)];
        if (col == 6) toggleFollow(q(jstr(c, "login")));
        else QDesktopServices::openUrl(QUrl(q(jstr(c, "profile"))));
    });
    connect(send, &QPushButton::clicked, this, &CommunityPage::submit);
    connect(ctx.ai->worker(), &AiWorker::jobDone, this, [this](const QString& op, const QString& result, const QString& err) {
        if (!op.startsWith("community_")) return;
        if (!err.isEmpty()) { Toast::popup(ctx_.window, err, false, 6000); refresh(); return; }
        Json r = Json::parse(s(result));
        if (op == "community_submit") Toast::popup(ctx_.window, "Build shared: " + q(jstr(r, "url")));
        else if (op == "community_vote") Toast::popup(ctx_.window, "Vote counted.");
        else if (op == "community_results")
            QMessageBox::information(this, "Everyone's results",
                                     QStringLiteral("%1 player(s) shared results:\n%2 matches · K/D %3 · win rate %4%")
                                         .arg(jinteger(r, "players")).arg(jinteger(r, "matches")).arg(jnum(r, "kd"), 0, 'f', 2).arg(jinteger(r, "win_rate")));
        refresh();
    });
}

void CommunityPage::refresh() {
    Community com(ctx_.engine.config, ctx_.engine.http);
    std::string repo = com.repo();
    Json cache = com.loadCache();
    if (repo.empty()) status_->setText("No community repo yet — press ⚙ Set up and enter owner/name of a GitHub repo (see community/README.md).");
    else
        status_->setText(QStringLiteral("Repo %1 · %2 · last refreshed %3")
                             .arg(q(repo), ctx_.engine.config.env("GITHUB_TOKEN").empty() ? QStringLiteral("read-only (no GITHUB_TOKEN)") : QStringLiteral("token set"),
                                  jnum(cache, "fetched") ? q(utcIso(static_cast<std::time_t>(jnum(cache, "fetched")))).left(16).replace('T', ' ') : QStringLiteral("never")));
    list_ = com.buildsFor(s(game_->currentData().toString()), s(sort_->currentData().toString()));
    builds_->setRowCount(int(list_.size()));
    for (int i = 0; i < int(list_.size()); ++i) {
        const Json& b = list_.elements()[size_t(i)];
        builds_->setItem(i, 0, cell(q(jstr(b, "weapon"))));
        builds_->setItem(i, 1, cell(q(gameById(jstr(b, "game")).label()), theme::current().muted));
        builds_->setItem(i, 2, cell(q(jstr(b, "author"))));
        builds_->setItem(i, 3, cell(QString::number(jinteger(b, "up")), theme::current().green));
        builds_->setItem(i, 4, cell(QString::number(jinteger(b, "down")), theme::current().red));
        builds_->setItem(i, 5, cell(QString::number(jnum(b, "score"), 'f', 2)));
        builds_->setItem(i, 6, cell(jbool(b, "verified") ? "✔ verified" : "", theme::current().green));
    }
    creatorList_ = com.creators(com.buildsFor());
    creators_->setRowCount(int(creatorList_.size()));
    for (int i = 0; i < int(creatorList_.size()); ++i) {
        const Json& c = creatorList_.elements()[size_t(i)];
        std::vector<std::string> g;
        for (auto& x : c.get("games").elements()) g.push_back(gameById(x.toString()).label());
        creators_->setItem(i, 0, cell(q(jstr(c, "name"))));
        creators_->setItem(i, 1, cell(QString::number(jinteger(c, "builds"))));
        creators_->setItem(i, 2, cell(QString::number(jinteger(c, "up"))));
        creators_->setItem(i, 3, cell(QString::number(jinteger(c, "down"))));
        creators_->setItem(i, 4, cell(QString::number(jnum(c, "score"), 'f', 2)));
        creators_->setItem(i, 5, cell(q(join(g, ", ")), theme::current().muted));
        creators_->setItem(i, 6, cell(jbool(c, "following") ? "★ following" : "☆ follow (double-click)", theme::current().accent));
    }
    if (!repo.empty() && static_cast<double>(nowSeconds()) - jnum(cache, "fetched") > 600 && !property("_asked").toBool()) {
        setProperty("_asked", true);  // fetch once when the page is first opened
        emit ctx_.ai->job("community_refresh", "{}");
        emit ctx_.ai->job("community_creators", "{}");
    }
}

Json CommunityPage::selected() const {
    auto rows = builds_->selectionModel()->selectedRows();
    if (rows.isEmpty() || rows.first().row() >= int(list_.size())) return Json();
    return list_.elements()[size_t(rows.first().row())];
}

void CommunityPage::showBuild() {
    Json b = selected();
    if (!b.isObject()) return;
    QString html = "<h2>" + esc(jstr(b, "weapon")) + "</h2><p>" + esc(gameById(jstr(b, "game")).name) + " · " + esc(jstr(b, "mode")) +
                   (jstr(b, "playstyle").empty() ? QString() : " · " + esc(jstr(b, "playstyle"))) + "</p><p>by <b>" +
                   esc(jstr(b, "author")) + "</b> · 👍 " + QString::number(jinteger(b, "up")) + " 👎 " + QString::number(jinteger(b, "down")) +
                   (jbool(b, "verified") ? " · ✔ verified" : "") + "</p><table cellpadding='4'>";
    for (auto& a : b.get("attachments").elements())
        html += "<tr><td style='color:" + theme::current().muted.name() + "'>" + esc(a.elements()[0].toString()) + "</td><td><b>" +
                esc(a.elements()[1].toString()) + "</b></td></tr>";
    html += "</table>";
    auto list = [](const Json& arr) {
        std::vector<std::string> v;
        for (auto& x : arr.elements()) v.push_back(x.toString());
        return q(join(v, ", ")).toHtmlEscaped();
    };
    if (b.get("perks").size()) html += "<p><b>Perks:</b> " + list(b.get("perks")) + "</p>";
    if (b.get("equipment").size()) html += "<p><b>Equipment:</b> " + list(b.get("equipment")) + "</p>";
    html += "<p><a href='" + q(jstr(b, "url")) + "'>" + esc(jstr(b, "title")) + "</a></p>";
    detail_->setHtml(html);
}

void CommunityPage::setup() {
    Json cfg = ctx_.engine.config.snapshot();
    bool ok = false;
    QString repo = QInputDialog::getText(this, "Community repo",
                                         "GitHub repo for your community's builds (owner/name). Create a public repo, then enter it here:",
                                         QLineEdit::Normal, q(jstr(cfg.get("community"), "repo")), &ok);
    if (!ok) return;
    cfg["community"]["repo"] = s(repo.trimmed());
    ctx_.engine.config.save(cfg);
    QString token = QInputDialog::getText(this, "GitHub token (optional)",
                                          "Optional: a fine-grained token with Issues read & write on that repo, to post and vote from the app.\n"
                                          "Leave empty to use the browser instead. It's saved only in your private settings file.",
                                          QLineEdit::Password, q(ctx_.engine.config.env("GITHUB_TOKEN")), &ok);
    if (ok) ctx_.engine.config.writeEnv({{"GITHUB_TOKEN", s(token.trimmed())}});
    setProperty("_asked", false);
    refresh();
}

void CommunityPage::toggleFollow(const QString& login) {
    Json cfg = ctx_.engine.config.snapshot();
    Json& f = cfg["community"]["following"];
    Json kept = Json::array();
    bool was = false;
    for (auto& x : f.elements()) { if (q(x.toString()) == login) was = true; else kept.push(x); }
    if (!was) kept.push(s(login));
    f = kept;
    ctx_.engine.config.save(cfg);
    Toast::popup(ctx_.window, was ? "Unfollowed " + login : "Following " + login + " — you'll get a push when they post a build.");
    refresh();
}

void CommunityPage::submit() {
    if (sWeapon_->text().trimmed().isEmpty()) { Toast::popup(ctx_.window, "Enter the weapon.", false); return; }
    Json b = Json::object();
    b["weapon"] = s(sWeapon_->text().trimmed());
    b["class"] = s(sClass_->text().trimmed());
    b["mode"] = s(sMode_->currentData().toString());
    b["playstyle"] = s(sStyle_->currentData().toString());
    b["range"] = s(sRange_->currentData().toString());
    b["input"] = s(sInput_->currentData().toString());
    b["season"] = s(sSeason_->text().trimmed());
    Json att = Json::array();
    for (const auto& l : sAttach_->toPlainText().split('\n')) {
        int c = int(l.indexOf(':'));
        if (c <= 0) continue;
        Json p = Json::array();
        p.push(s(l.left(c).trimmed()));
        p.push(s(l.mid(c + 1).trimmed()));
        att.push(p);
    }
    b["attachments"] = att;
    auto list = [](const QString& t) {
        Json a = Json::array();
        for (auto& x : t.split(',')) if (!x.trimmed().isEmpty()) a.push(s(x.trimmed()));
        return a;
    };
    b["perks"] = list(sPerks_->text());
    b["equipment"] = list(sEquip_->text());
    std::string game = s(gameOf(sGame_));
    Community com(ctx_.engine.config, ctx_.engine.http);
    if (com.repo().empty()) { setup(); return; }
    if (ctx_.engine.config.env("GITHUB_TOKEN").empty()) {
        try {
            QDesktopServices::openUrl(QUrl(q(com.submitUrl(b, game, s(sNote_->toPlainText())))));
            Toast::popup(ctx_.window, "GitHub opened with your build filled in — press Submit there.");
        } catch (const std::exception& e) {
            Toast::popup(ctx_.window, q(e.what()), false);
        }
        return;
    }
    Json a = Json::object();
    a["build"] = b;
    a["game"] = game;
    a["note"] = s(sNote_->toPlainText());
    emit ctx_.ai->job("community_submit", q(a.dump()));
}
