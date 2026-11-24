#include "pages.hpp"

#include <set>

#include <QApplication>
#include <algorithm>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QColorDialog>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "../core/checker.hpp"
#include "../core/discord.hpp"
#include "../core/games.hpp"
#include "../core/util.hpp"
#include "../core/community.hpp"
#include "../core/i18n.hpp"
#include "aiworker.hpp"
#include "uitranslate.hpp"
#include "themeeditor.hpp"
#include "art.hpp"
#include "../core/validation.hpp"
#include "autostart.hpp"
#include "codesmodel.hpp"
#include "redeem.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include "widgets.hpp"

using namespace hunter;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& x) { return x.toStdString(); }

QTableWidget* makeTable(const QStringList& headers) {
    auto* t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->setShowGrid(false);
    t->setWordWrap(false);
    t->verticalHeader()->setDefaultSectionSize(34);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}
QTableWidgetItem* cell(const QString& text, const QColor& color = QColor()) {
    auto* i = new QTableWidgetItem(text);
    i->setToolTip(text);
    if (color.isValid()) i->setForeground(color);
    return i;
}
QVBoxLayout* pageLayout(QWidget* w) {
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(28, 22, 28, 18);
    v->setSpacing(12);
    return v;
}
}  // namespace

// ════════════════════════════════ Codes ════════════════════════════════
CodesPage::CodesPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    auto* heroActions = new QWidget;
    auto* ah = new QHBoxLayout(heroActions);
    ah->setContentsMargins(0, 0, 0, 0);
    auto* add = button("＋ Add a code", "ghost");
    connect(add, &QPushButton::clicked, this, &CodesPage::addManual);
    auto* exp = button("⤓ Export CSV", "ghost");
    exp->setToolTip("Save the check report (every code with its verdict and reasons) for Excel / Sheets");
    connect(exp, &QPushButton::clicked, this, &CodesPage::exportCsv);
    ah->addWidget(exp);
    ah->addWidget(add);
    auto* hero = new HeroBanner("All games", "Every code the scanner has found, best first.", heroActions);
    title_ = hero->titleLabel();
    hint_ = hero->hintLabel();
    v->addWidget(hero);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);
    chips_ = new QButtonGroup(this);
    chips_->setExclusive(true);
    const std::pair<const char*, const char*> views[] = {{"ready", "Ready"}, {"fresh", "New"}, {"maybe", "Unconfirmed"},
                                                         {"expired", "Expired / used up"}, {"redeemed", "Redeemed"}, {"all", "All"}};
    for (auto& [key, text] : views) {
        auto* chip = button(text, "chip");
        chip->setProperty("view", key);
        chip->setChecked(QString(key) == view_);
        chips_->addButton(chip);
        bar->addWidget(chip);
    }
    connect(chips_, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* b) {
        view_ = b->property("view").toString();
        refresh();
    });
    bar->addStretch(1);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search codes…  (Ctrl+F)");
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(260);
    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    bar->addWidget(search_);
    v->addLayout(bar);

    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(12);
    auto* left = new QWidget;
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    model_ = new CodesModel(ctx.engine.store, this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setSortRole(CodesModel::SortRole);
    table_ = new QTableView;
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->sortByColumn(CodesModel::Verdict, Qt::AscendingOrder);  // likely-to-work first
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(36);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    const int widths[] = {170, 90, 220, 170, 70, 130, 55, 100};
    for (int c = 0; c < CodesModel::ColCount; ++c) table_->setColumnWidth(c, widths[c]);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &CodesPage::showDetail);
    connect(table_, &QTableView::doubleClicked, this, &CodesPage::redeemSelected);
    connect(table_, &QTableView::customContextMenuRequested, this, [this](const QPoint& p) {
        if (!selectedId()) return;
        QMenu m;
        m.addAction("Redeem…", this, &CodesPage::redeemSelected);
        m.addAction("Copy code", this, [this] { dCopy_->click(); });
        m.addSeparator();
        m.addAction("Mark: it worked", this, [this] { mark("SUCCESS"); });
        m.addAction("Mark: expired", this, [this] { mark("EXPIRED"); });
        m.addAction("Mark: used up (claim limit)", this, [this] { mark("LIMIT_REACHED"); });
        m.addAction("Mark: invalid", this, [this] { mark("INVALID"); });
        m.addSeparator();
        m.addAction("Hide this code", this, [this] { mark("IGNORED"); });
        m.exec(table_->viewport()->mapToGlobal(p));
    });
    lv->addWidget(table_);
    empty_ = label("", "PageHint");
    empty_->setAlignment(Qt::AlignCenter);
    lv->addWidget(empty_);
    split->addWidget(left);

    // detail panel
    detail_ = new QFrame;
    detail_->setObjectName("Detail");
    detail_->setMinimumWidth(320);
    auto* dv = new QVBoxLayout(detail_);
    dv->setContentsMargins(18, 18, 18, 18);
    dv->setSpacing(8);
    dCode_ = label("", "CodeBig");
    dMeta_ = label("", "Muted");
    dVerdict_ = label("");
    dVerdict_->setStyleSheet("font-size: 12pt; font-weight: 700;");
    dReward_ = label("");
    dReward_->setWordWrap(true);
    auto* actions = new QHBoxLayout;
    dRedeem_ = button("Redeem…", "accent");
    dCopy_ = button("📋 Copy");
    auto* more = button("Mark ▾");
    auto* markMenu = new QMenu(more);
    markMenu->addAction("✅ It worked", this, [this] { mark("SUCCESS"); });
    markMenu->addAction("🎁 Already redeemed", this, [this] { mark("ALREADY_REDEEMED"); });
    markMenu->addAction("⌛ Expired", this, [this] { mark("EXPIRED"); });
    markMenu->addAction("🚫 Used up (claim limit)", this, [this] { mark("LIMIT_REACHED"); });
    markMenu->addAction("❌ Invalid", this, [this] { mark("INVALID"); });
    markMenu->addSeparator();
    markMenu->addAction("🙈 Hide", this, [this] { mark("IGNORED"); });
    more->setMenu(markMenu);
    connect(dRedeem_, &QPushButton::clicked, this, &CodesPage::redeemSelected);
    connect(dCopy_, &QPushButton::clicked, this, [this] {
        if (auto* r = model_->rowAt(proxy_->mapToSource(table_->currentIndex()).row())) {
            QApplication::clipboard()->setText(q(r->code));
            Toast::popup(ctx_.window, QStringLiteral("Copied %1").arg(q(r->code)));
        }
    });
    actions->addWidget(dRedeem_);
    actions->addWidget(dCopy_);
    actions->addWidget(more);
    actions->addStretch(1);
    dHow_ = label("", "Muted");
    dReasons_ = label("");
    dReasons_->setWordWrap(true);
    dSightings_ = new QListWidget;
    dSightings_->setMinimumHeight(120);
    dSightings_->setToolTip("Double-click to open the page");
    connect(dSightings_, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        QString url = it->data(Qt::UserRole).toString();
        if (url.startsWith("https://")) QDesktopServices::openUrl(QUrl(url));
    });
    dv->addWidget(dCode_);
    dv->addWidget(dMeta_);
    dv->addWidget(dVerdict_);
    dv->addWidget(dReward_);
    dv->addLayout(actions);
    dv->addWidget(dHow_);
    dv->addWidget(label("Why", "SectionTitle"));
    dv->addWidget(dReasons_);
    dv->addWidget(label("Seen on", "SectionTitle"));
    dv->addWidget(dSightings_, 1);
    split->addWidget(detail_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    split->setSizes({760, 360});
    v->addWidget(split, 1);
    showDetail();
}

void CodesPage::setGame(const QString& gameId) {
    game_ = gameId;
    if (gameId.isEmpty()) {
        title_->setText("All games");
        hint_->setText("Every code the scanner has found, best first. Double-click a code to redeem it.");
    } else {
        const Game& g = gameById(s(gameId));
        title_->setText(q(g.name));
        hint_->setText(q(redeemInfo(g.id).second));
    }
    table_->setColumnHidden(CodesModel::Game, !gameId.isEmpty());
    refresh();
}

void CodesPage::setView(const QString& view) {
    view_ = view;
    for (auto* b : chips_->buttons()) b->setChecked(b->property("view").toString() == view);
    refresh();
}

void CodesPage::searchFor(const QString& text) {
    view_ = "all";
    for (auto* b : chips_->buttons()) b->setChecked(b->property("view").toString() == view_);
    search_->setText(text);
    search_->setFocus();
}

void CodesPage::focusSearch() {
    search_->setFocus();
    search_->selectAll();
}

long long CodesPage::selectedId() const {
    QModelIndex i = table_->currentIndex();
    if (!i.isValid() || table_->selectionModel()->selectedRows().isEmpty()) return 0;
    const CodeRow* r = model_->rowAt(proxy_->mapToSource(i).row());
    return r ? r->id : 0;
}

void CodesPage::refresh() {
    long long keep = selectedId();
    auto counts = ctx_.engine.store.viewCounts(s(game_));
    for (auto* b : chips_->buttons()) {
        QString key = b->property("view").toString();
        QString base = b->text().section("  ", 0, 0);
        int n = counts[s(key)];
        b->setText(n ? QStringLiteral("%1  %2").arg(base).arg(n) : base);
    }
    model_->reload(s(view_), s(search_->text().trimmed()), s(game_));
    if (keep) {
        int r = model_->rowOfId(keep);
        if (r >= 0) table_->selectRow(proxy_->mapFromSource(model_->index(r, 0)).row());
    }
    bool none = model_->rowCount() == 0;
    table_->setVisible(!none);
    empty_->setVisible(none);
    if (none) {
        empty_->setText(view_ == "ready" ? "Nothing ready to redeem right now.\nNew codes show up here the moment a site lists them."
                                         : "No codes here yet.");
    }
    showDetail();
}

void CodesPage::showDetail() {
    long long id = selectedId();
    auto row = id ? ctx_.engine.store.getCode(id) : std::nullopt;
    detail_->setEnabled(bool(row));
    if (!row) {
        dCode_->setText("—");
        dMeta_->setText("Pick a code to see where it was found and how likely it is to work.");
        for (auto* l : {dVerdict_, dReward_, dReasons_, dHow_}) l->clear();
        dSightings_->clear();
        return;
    }
    const Game& g = gameById(row->game);
    dCode_->setText(q(row->code));
    QString where = row->game == "codm" ? q(row->region) : (row->platform.empty() ? "All platforms" : q(row->platform));
    dMeta_->setText(QStringLiteral("%1 · %2 · confidence %3").arg(q(g.label()), where).arg(row->confidence));
    QString verdict = row->checkVerdict.empty() ? q(row->status) : q(statusText(*row));
    dVerdict_->setText(verdict);
    QColor col = row->checkVerdict.empty() ? theme::statusColor(q(row->status)) : theme::verdictColor(q(row->checkVerdict));
    dVerdict_->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700; color: %1;").arg(col.name()));
    std::string reward = bestReward(ctx_.engine.store.allSnippets(row->id), row->code);
    dReward_->setText(reward.empty() ? "" : "🎁 " + q(reward));
    dHow_->setText(q(redeemInfo(row->game, row->code, row->kind).second));
    QString reasons = q(row->checkNotes);
    dReasons_->setText(reasons.isEmpty() ? q(row->validationNotes) : reasons);
    dSightings_->clear();
    for (const auto& g2 : ctx_.engine.store.sightingsFor(row->id)) {
        QString icon = g2.report == "active" ? "🟢" : g2.report == "removed" ? "⚪" : "🔴";
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2 — %3").arg(icon, q(g2.sourceName.empty() ? g2.documentUrl : g2.sourceName),
                                                                        q(g2.report)));
        item->setToolTip(q(g2.snippet) + "\n\n" + q(g2.documentUrl));
        item->setData(Qt::UserRole, q(g2.documentUrl));
        dSightings_->addItem(item);
    }
    bool dead = isFinalStatus(row->status);
    dRedeem_->setEnabled(!dead && row->region != "Garena");
    dRedeem_->setToolTip(row->region == "Garena" ? "Garena codes are redeemed inside the game." : "");
}

void CodesPage::redeemSelected() {
    long long id = selectedId();
    if (!id) return;
    QString why;
    auto* w = RedeemWindow::open(ctx_.engine, id, ctx_.window, why);
    if (!w) {
        Toast::popup(ctx_.window, why, false, 5000);
        return;
    }
    connect(w, &RedeemWindow::resultRecorded, this, [this](long long, const QString& outcome) {
        Toast::popup(ctx_.window, QStringLiteral("Recorded: %1").arg(outcome.toLower().replace('_', ' ')));
        refresh();
        emit changed();
    });
}

void CodesPage::mark(const QString& status) {
    long long id = selectedId();
    if (!id) return;
    ctx_.engine.store.setStatus(id, s(status), "Marked by you");
    ctx_.engine.intel.forgetTrackRecord();
    checkOne(ctx_.engine.store, id);
    ctx_.engine.store.addAudit("MARKED", s(status));
    refresh();
    emit changed();
}

void CodesPage::addManual() {
    bool ok = false;
    QString code = QInputDialog::getText(this, "Add a code", "Code you saw yourself (in game, on stream, on social media):",
                                         QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || code.isEmpty()) return;
    std::string game = game_.isEmpty() ? (isCodmShape(s(code.toUpper())) ? "codm" : "other") : s(game_);
    auto [id, why] = ctx_.engine.intel.addManualCode(s(code), "Unknown", "Added manually", game);
    if (!id) {
        Toast::popup(ctx_.window, "Not added: " + q(join(why, "; ")), false, 5000);
        return;
    }
    checkOne(ctx_.engine.store, id);
    Toast::popup(ctx_.window, why.empty() ? "Code added." : q(why.front()));
    setView("all");
    emit changed();
}

void CodesPage::exportCsv() {
    QString base = game_.isEmpty() ? QStringLiteral("all-games") : game_;
    QString suggested = QDir::home().filePath(QStringLiteral("cod-codes-%1-%2.csv").arg(base, QDate::currentDate().toString(Qt::ISODate)));
    QString path = QFileDialog::getSaveFileName(this, "Export codes", suggested, "CSV (*.csv)");
    if (path.isEmpty()) return;
    checkAll(ctx_.engine.store);  // fresh verdicts
    auto rows = reportRows(ctx_.engine.store, true, s(game_));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Toast::popup(ctx_.window, "Couldn't save: " + f.errorString(), false);
        return;
    }
    f.write(QByteArray::fromStdString(toCsv(rows)));
    Toast::popup(ctx_.window, QStringLiteral("Exported %1 codes.").arg(rows.size()));
}

// ════════════════════════════════ Sources ════════════════════════════════
SourcesPage::SourcesPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    auto* actions = new QWidget;
    auto* ah = new QHBoxLayout(actions);
    ah->setContentsMargins(0, 0, 0, 0);
    auto* imp = button("⬇ Import", "ghost");
    imp->setToolTip("Add sources from a file someone shared");
    auto* exp = button("⬆ Export", "ghost");
    exp->setToolTip("Save your sources to a file to share or back up");
    auto* add = button("＋ Add a source", "accent");
    for (QWidget* w : std::initializer_list<QWidget*>{imp, exp, add}) ah->addWidget(w);
    connect(add, &QPushButton::clicked, this, &SourcesPage::addSource);
    connect(imp, &QPushButton::clicked, this, &SourcesPage::importSources);
    connect(exp, &QPushButton::clicked, this, &SourcesPage::exportSources);
    v->addWidget(pageHeader("Sources", "Where the scanner looks: code pages, RSS, Google News, Google Alerts and YouTube channels. "
                                       "It only reads public pages, follows robots.txt and backs off when a site asks.", actions));
    auto* bar = new QHBoxLayout;
    summary_ = label("", "PageHint");
    filter_ = new QLineEdit;
    filter_->setPlaceholderText("Filter sources…");
    filter_->setClearButtonEnabled(true);
    filter_->setFixedWidth(260);
    connect(filter_, &QLineEdit::textChanged, this, &SourcesPage::applyFilter);
    bar->addWidget(summary_, 1);
    bar->addWidget(filter_);
    v->addLayout(bar);
    table_ = makeTable({"ON", "SOURCE", "TYPE", "GAME", "TRUST", "EVERY", "LAST RESULT", "CODES FOUND"});
    table_->setColumnWidth(0, 46);
    table_->setColumnWidth(1, 300);
    table_->setColumnWidth(2, 100);
    table_->setColumnWidth(3, 80);
    table_->setColumnWidth(4, 140);
    table_->setColumnWidth(5, 80);
    table_->setColumnWidth(6, 230);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (!loading_ && it->column() == 0) toggled(it->row(), it->checkState() == Qt::Checked);
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) { editSource(r); });
    v->addWidget(table_, 1);
    auto* row = new QHBoxLayout;
    auto* edit = button("✎ Edit", "ghost");
    connect(edit, &QPushButton::clicked, this, [this] {
        auto rows = table_->selectionModel()->selectedRows();
        if (!rows.isEmpty()) editSource(rows.first().row());
    });
    auto* open = button("🔗 Open", "ghost");
    connect(open, &QPushButton::clicked, this, [this] {
        auto rows = table_->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        QString url = table_->item(rows.first().row(), 1)->data(Qt::UserRole).toString();
        if (url.startsWith("https://")) QDesktopServices::openUrl(QUrl(url));
    });
    auto* remove = button("🗑 Remove selected", "ghost");
    connect(remove, &QPushButton::clicked, this, &SourcesPage::removeSelected);
    for (QWidget* w : std::initializer_list<QWidget*>{edit, open, remove}) row->addWidget(w);
    row->addStretch(1);
    row->addWidget(label("Double-click a source to edit it.", "Muted"));
    v->addLayout(row);
}

void SourcesPage::refresh() {
    loading_ = true;
    Json cfg = ctx_.engine.config.snapshot();
    std::map<std::string, SourceRow> health;
    for (auto& r : ctx_.engine.store.listSources()) health[r.url] = r;
    const auto& list = cfg.get("sources").elements();
    table_->setRowCount(int(list.size()));
    int on = 0;
    for (int i = 0; i < int(list.size()); ++i) {
        const Json& src = list[size_t(i)];
        auto* check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        bool enabled = jbool(src, "enabled");
        on += enabled;
        check->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        table_->setItem(i, 0, check);
        std::string where = jstr(src, "url", jstr(src, "query", jstr(src, "channel")));
        auto* name = cell(q(jstr(src, "name")));
        name->setData(Qt::UserRole, q(jstr(src, "url")));
        name->setToolTip(q(jstr(src, "notes")) + "\n" + q(where));
        table_->setItem(i, 1, name);
        table_->setItem(i, 2, cell(q(jstr(src, "type", "web")).replace('_', ' '), theme::current().muted));
        std::string game = jstr(src, "game", "auto");
        table_->setItem(i, 3, cell(game == "auto" || game.empty() ? "any" : q(gameById(game).label())));
        table_->setItem(i, 4, cell(q(jstr(src, "trust")).replace('_', ' '), theme::current().muted));
        long long every = jinteger(src, "interval_minutes", 0);
        table_->setItem(i, 5, cell(every ? QStringLiteral("%1 min").arg(every) : "each scan", theme::current().muted));
        auto h = health.find(jstr(src, "url"));
        QString last = "—";
        QColor col = theme::current().muted;
        int found = 0;
        if (h != health.end()) {
            last = q(h->second.lastStatus) + "  ·  " + q(h->second.lastChecked).left(16).replace('T', ' ');
            col = startsWith(h->second.lastStatus, "OK") || h->second.lastStatus == "NOT_MODIFIED" ? theme::current().green
                  : startsWith(h->second.lastStatus, "BLOCKED") || startsWith(h->second.lastStatus, "ROBOTS") ? theme::current().yellow
                                                                                                               : theme::current().red;
            found = h->second.codesFound;
        }
        table_->setItem(i, 6, cell(last, col));
        table_->setItem(i, 7, cell(QString::number(found)));
    }
    summary_->setText(QStringLiteral("%1 of %2 sources on.").arg(on).arg(list.size()));
    loading_ = false;
    applyFilter();
}

void SourcesPage::applyFilter() {
    const QString f = filter_->text().trimmed();
    for (int r = 0; r < table_->rowCount(); ++r) {
        bool show = f.isEmpty();
        for (int c = 1; c <= 4 && !show; ++c)
            if (auto* it = table_->item(r, c)) show = it->text().contains(f, Qt::CaseInsensitive) || it->toolTip().contains(f, Qt::CaseInsensitive);
        table_->setRowHidden(r, !show);
    }
}

void SourcesPage::toggled(int row, bool on) {
    Json cfg = ctx_.engine.config.snapshot();
    auto& list = cfg["sources"].elements();
    if (row < 0 || row >= int(list.size())) return;
    list[size_t(row)]["enabled"] = on;
    ctx_.engine.config.save(cfg);
    refresh();
}

void SourcesPage::removeSelected() {
    auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    if (QMessageBox::question(this, "Remove sources", QStringLiteral("Remove %1 source(s)?").arg(rows.size())) != QMessageBox::Yes) return;
    std::set<int> drop;
    for (auto& r : rows) drop.insert(r.row());
    Json cfg = ctx_.engine.config.snapshot();
    Json::Array kept;
    const auto& list = cfg.get("sources").elements();
    for (int i = 0; i < int(list.size()); ++i) if (!drop.count(i)) kept.push_back(list[size_t(i)]);
    cfg["sources"] = Json(kept);
    ctx_.engine.config.save(cfg);
    refresh();
}

// The add / edit form. Returns false when cancelled or invalid.
bool SourcesPage::sourceDialog(Json& src, const QString& title) {
    QDialog d(this);
    d.setWindowTitle(title);
    d.resize(560, 0);
    auto* f = new QFormLayout(&d);
    f->setContentsMargins(20, 20, 20, 20);
    auto* type = new QComboBox;
    type->addItem("Web page with codes", "web");
    type->addItem("RSS / Atom feed", "rss");
    type->addItem("Google News search", "google_news");
    type->addItem("Google Alerts RSS feed", "google_alerts");
    type->addItem("YouTube channel", "youtube");
    type->setCurrentIndex(std::max(0, type->findData(q(jstr(src, "type", "web")))));
    auto* name = new QLineEdit(q(jstr(src, "name")));
    auto* url = new QLineEdit(q(jstr(src, "url", jstr(src, "query", jstr(src, "channel")))));
    url->setPlaceholderText("https://…  (for YouTube: @handle, for Google News: search words)");
    auto* game = new QComboBox;
    game->addItem("Detect from the page", "auto");
    for (const auto& g : allGames()) if (g.id != "other") game->addItem(q(g.name), q(g.id));
    game->setCurrentIndex(std::max(0, game->findData(q(jstr(src, "game", "auto")))));
    auto* trust = new QComboBox;
    for (const auto& t : trustLevels()) trust->addItem(q(t).replace('_', ' '), q(t));
    int ti = trust->findData(q(jstr(src, "trust", "community")));
    trust->setCurrentIndex(ti < 0 ? 2 : ti);
    auto* every = new QSpinBox;
    every->setRange(0, 1440);
    every->setSuffix(" min");
    every->setSpecialValueText("every scan");
    every->setValue(int(jinteger(src, "interval_minutes", 30)));
    auto* regions = new QComboBox;
    regions->addItem("Both regions", "both");
    regions->addItem("Global only", "Global");
    regions->addItem("Garena only", "Garena");
    {
        std::vector<std::string> rg;
        for (auto& x : src.get("regions").elements()) rg.push_back(x.toString());
        regions->setCurrentIndex(rg.size() == 1 ? std::max(0, regions->findData(q(rg[0]))) : 0);
    }
    auto* notes = new QLineEdit(q(jstr(src, "notes")));
    auto* enabled = new QCheckBox("On");
    enabled->setChecked(jbool(src, "enabled", true));
    f->addRow("Type", type);
    f->addRow("Name", name);
    f->addRow("Address", url);
    f->addRow("Game", game);
    f->addRow("Trust", trust);
    f->addRow("Read it every", every);
    f->addRow("Region (CODM)", regions);
    f->addRow("Notes", notes);
    f->addRow("", enabled);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return false;
    const QString t = type->currentData().toString(), u = url->text().trimmed();
    if (u.isEmpty()) { Toast::popup(ctx_.window, "Enter the address.", false); return false; }
    for (const char* k : {"url", "query", "channel"}) src.items().erase(
        std::remove_if(src.items().begin(), src.items().end(), [k](const auto& kv) { return kv.first == k; }), src.items().end());
    src["name"] = s(name->text().trimmed().isEmpty() ? u : name->text().trimmed());
    src["type"] = s(t);
    if (t == "google_news") src["query"] = s(u);
    else if (t == "youtube") src["channel"] = s(u);
    else {
        if (!u.startsWith("https://")) { Toast::popup(ctx_.window, "Only https:// addresses are allowed.", false); return false; }
        src["url"] = s(u);
    }
    src["game"] = s(game->currentData().toString());
    src["trust"] = s(trust->currentData().toString());
    src["enabled"] = enabled->isChecked();
    src["interval_minutes"] = every->value();
    Json rg = Json::array();
    if (regions->currentData().toString() == "both") { rg.push("Global"); rg.push("Garena"); }
    else rg.push(s(regions->currentData().toString()));
    src["regions"] = rg;
    src["notes"] = s(notes->text().trimmed());
    return true;
}

void SourcesPage::addSource() {
    Json src = Json::object();
    src["interval_minutes"] = 30;
    if (!sourceDialog(src, "Add a source")) return;
    Json cfg = ctx_.engine.config.snapshot();
    cfg["sources"].push(src);
    ctx_.engine.config.save(cfg);
    refresh();
    Toast::popup(ctx_.window, "Source added — it's read on the next scan.");
}

void SourcesPage::editSource(int row) {
    Json cfg = ctx_.engine.config.snapshot();
    auto& list = cfg["sources"].elements();
    if (row < 0 || row >= int(list.size())) return;
    Json src = list[size_t(row)];
    if (!sourceDialog(src, "Edit source")) return;
    list[size_t(row)] = src;
    ctx_.engine.config.save(cfg);
    refresh();
    Toast::popup(ctx_.window, "Source saved.");
}

void SourcesPage::exportSources() {
    QString path = QFileDialog::getSaveFileName(this, "Export sources", "cod-hunter-sources.json", "Sources (*.json)");
    if (path.isEmpty()) return;
    Json out = Json::object();
    out["kind"] = "cod-redemption-hunter-sources";
    out["version"] = 1;
    out["sources"] = ctx_.engine.config.snapshot().get("sources");
    try {
        writeFileAtomic(fs::path(path.toStdWString()), out.dump(2));
        Toast::popup(ctx_.window, "Sources saved — anyone can import the file on their Sources page.");
    } catch (const std::exception& e) {
        Toast::popup(ctx_.window, q(e.what()), false);
    }
}

void SourcesPage::importSources() {
    QString path = QFileDialog::getOpenFileName(this, "Import sources", QString(), "Sources (*.json)");
    if (path.isEmpty()) return;
    Json data;
    try { data = Json::parse(readFile(fs::path(path.toStdWString()))); } catch (const std::exception& e) {
        Toast::popup(ctx_.window, QStringLiteral("Not a sources file: %1").arg(e.what()), false);
        return;
    }
    const Json list = data.isArray() ? data : data.get("sources");
    Json cfg = ctx_.engine.config.snapshot();
    std::set<std::string> have;
    for (auto& x : cfg.get("sources").elements()) have.insert(jstr(x, "url", jstr(x, "query", jstr(x, "channel"))));
    int added = 0, skipped = 0;
    for (auto& x : list.elements()) {
        std::string where = jstr(x, "url", jstr(x, "query", jstr(x, "channel")));
        const std::string type = jstr(x, "type", "web");
        bool ok = x.isObject() && !where.empty() && !jstr(x, "name").empty() &&
                  (type == "google_news" || type == "youtube" || startsWith(where, "https://")) && !have.count(where);
        if (!ok) { ++skipped; continue; }
        Json src = x;
        src["enabled"] = jbool(x, "enabled", true);
        if (std::find(trustLevels().begin(), trustLevels().end(), jstr(x, "trust")) == trustLevels().end()) src["trust"] = "unverified";
        cfg["sources"].push(src);
        have.insert(where);
        ++added;
    }
    ctx_.engine.config.save(cfg);
    refresh();
    Toast::popup(ctx_.window, QStringLiteral("%1 source(s) added, %2 skipped (duplicates or not https).").arg(added).arg(skipped));
}

// ════════════════════════════════ Activity ════════════════════════════════
ActivityPage::ActivityPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    v->addWidget(pageHeader("Activity", "Every scan, Discord post, redemption and setting change."));
    auto* tabs = new QTabWidget;
    scans_ = makeTable({"TIME (UTC)", "SOURCE", "RESULT", "DETAILS"});
    audit_ = makeTable({"TIME (UTC)", "EVENT", "DETAILS"});
    attempts_ = makeTable({"STARTED (UTC)", "CODE", "RESULT", "HOW", "COUNTED"});
    scans_->setColumnWidth(0, 150);
    scans_->setColumnWidth(1, 260);
    scans_->setColumnWidth(2, 90);
    audit_->setColumnWidth(0, 150);
    audit_->setColumnWidth(1, 200);
    attempts_->setColumnWidth(0, 150);
    attempts_->setColumnWidth(1, 170);
    tabs->addTab(scans_, "Scans");
    tabs->addTab(audit_, "Events");
    tabs->addTab(attempts_, "Redemptions");
    v->addWidget(tabs, 1);
}

void ActivityPage::refresh() {
    const Palette& p = theme::current();
    auto when = [](const std::string& iso) { return q(iso).left(19).replace('T', ' '); };
    auto scans = ctx_.engine.store.recentScans(300);
    scans_->setRowCount(int(scans.size()));
    for (int i = 0; i < int(scans.size()); ++i) {
        const auto& r = scans[size_t(i)];
        QColor c = r.status == "OK" ? p.green : r.status == "ERROR" ? p.red : p.yellow;
        scans_->setItem(i, 0, cell(when(r.ts), p.muted));
        scans_->setItem(i, 1, cell(q(r.name)));
        scans_->setItem(i, 2, cell(q(r.status), c));
        scans_->setItem(i, 3, cell(q(r.details)));
    }
    auto audit = ctx_.engine.store.recentAudit(300);
    audit_->setRowCount(int(audit.size()));
    for (int i = 0; i < int(audit.size()); ++i) {
        audit_->setItem(i, 0, cell(when(audit[size_t(i)].ts), p.muted));
        audit_->setItem(i, 1, cell(q(audit[size_t(i)].status)));
        audit_->setItem(i, 2, cell(q(audit[size_t(i)].details)));
    }
    auto att = ctx_.engine.store.recentAttempts(200);
    attempts_->setRowCount(int(att.size()));
    for (int i = 0; i < int(att.size()); ++i) {
        const auto& a = att[size_t(i)];
        attempts_->setItem(i, 0, cell(when(a.startedAt), p.muted));
        attempts_->setItem(i, 1, cell(q(a.code)));
        attempts_->setItem(i, 2, cell(q(a.outcome), theme::statusColor(q(a.outcome))));
        attempts_->setItem(i, 3, cell(q(a.method)));
        attempts_->setItem(i, 4, cell(a.counted ? "yes" : "no"));
    }
}

// ════════════════════════════════ Discord ════════════════════════════════
DiscordPage::DiscordPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* inner = new QWidget;
    auto* v = pageLayout(inner);
    v->addWidget(pageHeader("Discord", "Post codes to your server as colour-coded cards: the current list when you connect, "
                                       "new codes the moment they appear, and an update when one dies."));
    auto* hookBox = new QGroupBox("Webhook");
    auto* hf = new QFormLayout(hookBox);
    webhook_ = new QLineEdit;
    webhook_->setEchoMode(QLineEdit::Password);
    webhook_->setPlaceholderText("https://discord.com/api/webhooks/…");
    auto* show = new QCheckBox("Show");
    connect(show, &QCheckBox::toggled, this, [this](bool on) { webhook_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    auto* hookRow = new QHBoxLayout;
    hookRow->addWidget(webhook_, 1);
    hookRow->addWidget(show);
    hf->addRow("Codes channel", hookRow);
    systemHook_ = new QLineEdit;
    systemHook_->setEchoMode(QLineEdit::Password);
    systemHook_->setPlaceholderText("optional — app alerts (blocked sites, emergency stop)");
    hf->addRow("Alerts channel", systemHook_);
    hf->addRow(label("In Discord: Server Settings → Integrations → Webhooks → New Webhook → Copy Webhook URL. "
                     "Keep it private — anyone with it can post to your channel.", "Muted"));
    auto* testRow = new QHBoxLayout;
    auto* test = button("Send a test card");
    auto* list = button("Post the current list now");
    testRow->addWidget(test);
    testRow->addWidget(list);
    testRow->addStretch(1);
    hf->addRow(testRow);
    connect(test, &QPushButton::clicked, this, [this] { ctx_.scanner.testDiscord(webhook_->text().trimmed()); });
    connect(list, &QPushButton::clicked, this, [this] { save(); ctx_.scanner.postList(false); });
    v->addWidget(hookBox);

    auto* opts = new QGroupBox("What to post");
    auto* of = new QFormLayout(opts);
    enabled_ = new QCheckBox("Post to Discord");
    postNew_ = new QCheckBox("New codes as soon as they appear");
    updates_ = new QCheckBox("A follow-up when a posted code expires or is used up");
    onConnect_ = new QCheckBox("The current list when a webhook is first connected");
    garena_ = new QCheckBox("Include Garena (in-game) codes");
    which_ = new QComboBox;
    which_->addItem("Everything that isn't dead", "all");
    which_->addItem("Only codes that likely work", "likely");
    mention_ = new QLineEdit;
    mention_->setPlaceholderText("nobody · @everyone · @here · or a role ID");
    username_ = new QLineEdit;
    for (auto* c : {enabled_, postNew_, updates_, onConnect_, garena_}) of->addRow(c);
    of->addRow("New codes to post", which_);
    of->addRow("Mention", mention_);
    of->addRow("Bot name", username_);
    v->addWidget(opts);

    auto* per = new QGroupBox("Per game");
    auto* pg = new QGridLayout(per);
    pg->setHorizontalSpacing(14);
    pg->addWidget(label("Each game can post to its own channel. Leave a webhook empty to use the one above.", "Muted"), 0, 0, 1, 3);
    int r = 1;
    for (const auto& id : currentTitles()) {
        GameRow row{id, new QCheckBox(q(gameById(id).name)), new QLineEdit};
        row.hook->setEchoMode(QLineEdit::Password);
        row.hook->setPlaceholderText("same as the codes channel");
        pg->addWidget(row.post, r, 0);
        pg->addWidget(row.hook, r, 1, 1, 2);
        games_.push_back(row);
        ++r;
    }
    pg->setColumnStretch(1, 1);
    v->addWidget(per);
    auto* saveBtn = button("Save", "accent");
    connect(saveBtn, &QPushButton::clicked, this, [this] { save(); Toast::popup(ctx_.window, "Discord settings saved."); });
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(saveBtn);
    v->addLayout(row);
    v->addStretch(1);
    scroll->setWidget(inner);
    outer->addWidget(scroll);
}

void DiscordPage::refresh() {
    webhook_->setText(q(ctx_.engine.config.env("DISCORD_WEBHOOK_URL")));
    systemHook_->setText(q(ctx_.engine.config.env("DISCORD_WEBHOOK_SYSTEM")));
    Json d = ctx_.engine.config.snapshot().get("discord");
    enabled_->setChecked(jbool(d, "enabled", true));
    postNew_->setChecked(jbool(d, "post_new", true));
    updates_->setChecked(jbool(d, "post_updates", true));
    onConnect_->setChecked(jbool(d, "post_list_on_connect", true));
    garena_->setChecked(jbool(d, "include_garena", true));
    which_->setCurrentIndex(jstr(d, "new_codes", "all") == "likely" ? 1 : 0);
    mention_->setText(q(jstr(d, "mention")));
    username_->setText(q(jstr(d, "username", "COD Redemption Hunter")));
    for (auto& g : games_) {
        const Json& on = d.get("games").get(g.id);
        g.post->setChecked(!(on.isBool() && !on.toBool()));
        g.hook->setText(q(ctx_.engine.config.env("DISCORD_WEBHOOK_" + upper(g.id))));
    }
}

void DiscordPage::save() {
    QString hook = webhook_->text().trimmed(), sys = systemHook_->text().trimmed();
    if (!hook.isEmpty() && !validWebhook(s(hook))) {
        Toast::popup(ctx_.window, "That isn't a Discord webhook URL.", false);
        return;
    }
    std::map<std::string, std::string> env{{"DISCORD_WEBHOOK_URL", s(hook)}, {"DISCORD_WEBHOOK_SYSTEM", s(sys)}};
    for (auto& g : games_) {
        QString h = g.hook->text().trimmed();
        if (!h.isEmpty() && !validWebhook(s(h))) {
            Toast::popup(ctx_.window, q(gameById(g.id).label()) + ": that isn't a Discord webhook URL.", false);
            return;
        }
        env["DISCORD_WEBHOOK_" + upper(g.id)] = s(h);
    }
    ctx_.engine.config.writeEnv(env);
    Json cfg = ctx_.engine.config.snapshot();
    Json& d = cfg["discord"];
    d["enabled"] = enabled_->isChecked();
    d["post_new"] = postNew_->isChecked();
    d["post_updates"] = updates_->isChecked();
    d["post_list_on_connect"] = onConnect_->isChecked();
    d["include_garena"] = garena_->isChecked();
    d["new_codes"] = s(which_->currentData().toString());
    d["mention"] = s(mention_->text().trimmed());
    d["username"] = s(username_->text().trimmed().isEmpty() ? "COD Redemption Hunter" : username_->text().trimmed());
    for (auto& g : games_) d["games"][g.id] = g.post->isChecked();
    ctx_.engine.config.save(cfg);
}

// ════════════════════════════════ Account ════════════════════════════════
AccountPage::AccountPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* v = pageLayout(this);
    auto* edit = button("Edit in Settings", "ghost");
    connect(edit, &QPushButton::clicked, this, &AccountPage::openSettings);
    v->addWidget(pageHeader("Account & protection",
                            "Safety rules that keep your account out of trouble. They're stored in your database, so "
                            "restarting the app never resets them.", edit));

    auto* grid = new QGridLayout;
    grid->setSpacing(12);
    auto card = [](const QString& title, QLabel*& value) {
        auto* f = new QFrame;
        f->setObjectName("Card");
        auto* l = new QVBoxLayout(f);
        l->setContentsMargins(16, 12, 16, 12);
        l->addWidget(label(title, "CardLbl"));
        value = label("—");
        value->setWordWrap(true);
        value->setStyleSheet("font-size: 11pt; font-weight: 600;");
        l->addWidget(value);
        return f;
    };
    grid->addWidget(card("Player UID", uid_), 0, 0);
    grid->addWidget(card("Account binding", binding_), 0, 1);
    grid->addWidget(card("Redemptions today", limits_), 1, 0);
    grid->addWidget(card("Redemption pause (circuit breaker)", breaker_), 1, 1);
    v->addLayout(grid);

    auto* actions = new QHBoxLayout;
    auto* rebind = button("Re-bind to the UID in Settings", "ghost");
    auto* clear = button("Clear the pause", "ghost");
    connect(rebind, &QPushButton::clicked, this, [this] {
        bool ok = ctx_.engine.protection.binding.rebind();
        Toast::popup(ctx_.window, ok ? "Bound to the UID in Settings." : "Enter a valid UID in Settings first.", ok);
        refresh();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Clear the pause",
                                  "Only clear it once you know why redemption was paused (see the reason). Continue?") !=
            QMessageBox::Yes)
            return;
        ctx_.engine.protection.breaker.close();
        Toast::popup(ctx_.window, "Pause cleared.");
        refresh();
    });
    actions->addWidget(rebind);
    actions->addWidget(clear);
    actions->addStretch(1);
    v->addLayout(actions);

    auto* test = new QGroupBox("Official redemption pages");
    auto* tv = new QVBoxLayout(test);
    tv->addWidget(label("Loads the official page invisibly and checks the UID and code boxes are where the app expects them. "
                        "Nothing is typed or submitted.", "Muted"));
    auto* tr = new QHBoxLayout;
    auto* g = button("Test Global page");
    auto* ga = button("Test Garena page");
    auto* open = button("Open in browser", "ghost");
    connect(g, &QPushButton::clicked, this, [this] { testPage("Global"); });
    connect(ga, &QPushButton::clicked, this, [this] { testPage("Garena"); });
    connect(open, &QPushButton::clicked, this, [this] {
        QString url = q(ctx_.engine.config.redemptionUrl(""));
        QDesktopServices::openUrl(QUrl(url.isEmpty() ? QString(kOfficialCodmRedemptionUrl) : url));
    });
    tr->addWidget(g);
    tr->addWidget(ga);
    tr->addWidget(open);
    tr->addStretch(1);
    tv->addLayout(tr);
    pageResult_ = label("", "PageHint");
    tv->addWidget(pageResult_);
    v->addWidget(test);

    v->addWidget(label("Recent redemptions", "SectionTitle"));
    history_ = makeTable({"STARTED (UTC)", "CODE", "RESULT", "HOW", "COUNTS TOWARD CAP"});
    history_->setColumnWidth(0, 150);
    history_->setColumnWidth(1, 170);
    history_->setColumnWidth(2, 150);
    history_->setColumnWidth(3, 140);
    v->addWidget(history_, 1);
}

void AccountPage::refresh() {
    const Palette& p = theme::current();
    auto& prot = ctx_.engine.protection;
    uid_->setText(prot.binding.configured() ? q(prot.binding.masked()) : "Not set — add it in Settings");
    auto b = prot.binding.check();
    binding_->setText((b.first ? "● " : "▲ ") + q(b.second));
    binding_->setStyleSheet(QStringLiteral("font-size: 11pt; font-weight: 600; color: %1;").arg((b.first ? p.green : p.yellow).name()));
    int used = prot.limiter.usedToday(), cap = prot.limiter.dailyCap();
    double cool = prot.limiter.cooldownRemaining();
    limits_->setText(QStringLiteral("%1 of %2 used  ·  %3").arg(used).arg(cap)
                         .arg(cool > 0 ? QStringLiteral("next allowed in %1 min").arg(qRound(cool)) : QStringLiteral("no cooldown")));
    auto st = prot.breaker.state();
    breaker_->setText(st.open ? QStringLiteral("▲ Paused: %1%2").arg(q(st.reason), st.until.empty() ? QString(" (until you clear it)")
                                                                                                      : " (until " + q(st.until) + " UTC)")
                              : QStringLiteral("● Not paused"));
    breaker_->setStyleSheet(QStringLiteral("font-size: 11pt; font-weight: 600; color: %1;").arg((st.open ? p.red : p.green).name()));
    auto att = ctx_.engine.store.recentAttempts(50);
    history_->setRowCount(int(att.size()));
    for (int i = 0; i < int(att.size()); ++i) {
        const auto& a = att[size_t(i)];
        history_->setItem(i, 0, cell(q(a.startedAt).left(19).replace('T', ' '), p.muted));
        history_->setItem(i, 1, cell(q(a.code)));
        history_->setItem(i, 2, cell(q(a.outcome), theme::statusColor(q(a.outcome))));
        history_->setItem(i, 3, cell(q(a.method)));
        history_->setItem(i, 4, cell(a.counted ? "yes" : "no"));
    }
}

void AccountPage::testPage(const QString& region) {
    QString url = q(ctx_.engine.config.redemptionUrl(s(region)));
    if (url.isEmpty() && region == "Global") url = kOfficialCodmRedemptionUrl;
    if (!url.startsWith("https://")) {
        pageResult_->setText("No " + region + " redemption page is set (Settings → Account).");
        return;
    }
    pageResult_->setText("Loading " + url + " …");
    Json cfg = ctx_.engine.config.snapshot();
    auto list = [](const Json& arr) {
        QStringList out;
        for (const auto& x : arr.elements())
            if (x.toString().find(":has-text") == std::string::npos) out << q(x.toString());
        return out;
    };
    QStringList uidSel = list(cfg.get("redemption").get("selectors").get("uid"));
    QStringList codeSel = list(cfg.get("redemption").get("selectors").get("code"));
    QPointer<QLabel> out = pageResult_;
    checkOfficialPage(ctx_.renderer ? ctx_.renderer->profile() : nullptr, url, uidSel, codeSel, this,
                      [out, url, region, this](const PageCheck& r) {
                          if (!out) return;
                          if (!r.error.isEmpty()) {
                              out->setText("▲ " + r.error + "  Use “Open in browser” to check it yourself.");
                              return;
                          }
                          bool good = r.uidBoxes > 0 && r.codeBoxes > 0;
                          out->setText(QStringLiteral("%1 %2 page “%3”: UID box %4, code box %5, CAPTCHA %6.%7")
                                           .arg(QString::fromUtf8(good ? "●" : "▲"), region, r.title.left(60),
                                                QString::fromUtf8(r.uidBoxes ? "found" : "not found"),
                                                QString::fromUtf8(r.codeBoxes ? "found" : "not found"),
                                                QString::fromUtf8(r.captchas ? "present" : "not seen"),
                                                QString::fromUtf8(good ? "" : "  The page may have changed — redemption will copy the code so you can paste it.")));
                          ctx_.engine.store.addAudit("PAGE_TEST", s(region) + ": uid=" + std::to_string(r.uidBoxes) +
                                                                      " code=" + std::to_string(r.codeBoxes));
                          Q_UNUSED(url);
                      });
}

// ════════════════════════════════ Settings ════════════════════════════════
SettingsPage::SettingsPage(AppContext& ctx, QWidget* parent) : Page(ctx, parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll_ = scroll;
    scroll->setWidgetResizable(true);
    auto* inner = new QWidget;
    auto* v = pageLayout(inner);
    v->addWidget(pageHeader("Settings", "Your account, how often to scan, and how the app looks. Private values stay on this PC."));

    auto* acc = new QGroupBox("Call of Duty: Mobile account");
    auto* af = new QFormLayout(acc);
    uid_ = new QLineEdit;
    uid_->setPlaceholderText("Your Player UID — digits only (Profile → copy UID in game)");
    uid_->setValidator(new QRegularExpressionValidator(QRegularExpression("\\d{0,25}"), uid_));
    region_ = new QComboBox;
    region_->addItems({"Both", "Global", "Garena"});
    urlGlobal_ = new QLineEdit;
    urlGlobal_->setPlaceholderText(kOfficialCodmRedemptionUrl);
    urlGarena_ = new QLineEdit;
    urlGarena_->setPlaceholderText("optional");
    af->addRow("Player UID", uid_);
    af->addRow("Account region", region_);
    af->addRow("Global redemption page", urlGlobal_);
    af->addRow("Garena redemption page", urlGarena_);
    protection_ = label("", "Muted");
    af->addRow("Protection", protection_);
    auto* protRow = new QHBoxLayout;
    auto* rebind = button("Re-bind to this UID", "ghost");
    auto* clear = button("Clear redemption pause", "ghost");
    protRow->addWidget(rebind);
    protRow->addWidget(clear);
    protRow->addStretch(1);
    af->addRow(protRow);
    connect(rebind, &QPushButton::clicked, this, [this] {
        save();
        bool ok = ctx_.engine.protection.binding.rebind();
        Toast::popup(ctx_.window, ok ? "Bound to the new UID." : "Enter a valid UID first.", ok);
        refresh();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        ctx_.engine.protection.breaker.close();
        Toast::popup(ctx_.window, "Redemption pause cleared.");
        refresh();
    });
    v->addWidget(acc);

    auto* scan = new QGroupBox("Scanning");
    auto* sf = new QFormLayout(scan);
    interval_ = new QSpinBox;
    interval_->setRange(2, 240);
    interval_->setSuffix(" min");
    onLaunch_ = new QCheckBox("Start scanning when the app opens");
    autostart_ = new QCheckBox("Start with Windows (in the tray, scanning 24/7)");
    tray_ = new QCheckBox("Closing the window keeps scanning in the tray");
    notify_ = new QCheckBox("Desktop notifications for new codes");
    awake_ = new QCheckBox("Keep the PC awake while scanning");
    sf->addRow("Scan every", interval_);
    for (auto* c : {onLaunch_, autostart_, tray_, notify_, awake_}) sf->addRow(c);
    v->addWidget(scan);

    looks_ = new QGroupBox("Theme");
    auto* lv = new QVBoxLayout(looks_);
    lv->addWidget(label("Pick a look, or make your own: colours, camo pattern, corners, text size and the weapon on the page "
                        "banners. Themes are small files you can share.", "Muted"));
    tiles_ = new QWidget;
    new QGridLayout(tiles_);
    lv->addWidget(tiles_);
    auto* trow = new QHBoxLayout;
    auto* custom = button("🎨 Customize this theme", "accent");
    auto* imp = button("⬇ Import…", "ghost");
    auto* expo = button("⬆ Export…", "ghost");
    deleteTheme_ = button("🗑 Delete", "ghost");
    for (QWidget* w : std::initializer_list<QWidget*>{custom, imp, expo, deleteTheme_}) trow->addWidget(w);
    trow->addStretch(1);
    lv->addLayout(trow);
    connect(custom, &QPushButton::clicked, this, [this] {
        ThemeEditor ed(theme::current(), this);
        if (ed.exec() == QDialog::Accepted) emit themeChosen(ed.savedName());
        rebuildThemeTiles();
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Import a theme", QString(), "Theme (*.json)");
        if (f.isEmpty()) return;
        QString err, name = theme::importFile(f, &err);
        if (name.isEmpty()) { Toast::popup(ctx_.window, err, false); return; }
        emit themeChosen(name);
        rebuildThemeTiles();
    });
    connect(expo, &QPushButton::clicked, this, [this] {
        const Palette& p = theme::current();
        QString f = QFileDialog::getSaveFileName(this, "Export the theme", p.name + ".json", "Theme (*.json)");
        if (f.isEmpty()) return;
        QString err;
        bool ok = theme::exportFile(p.name, f, &err);
        Toast::popup(ctx_.window, ok ? QStringLiteral("Theme saved — share the file, others import it in Settings.") : err, ok);
    });
    connect(deleteTheme_, &QPushButton::clicked, this, [this] {
        const QString n = theme::current().name;
        if (theme::isBuiltin(n)) return;
        if (QMessageBox::question(this, "Delete theme", "Delete the theme \"" + theme::current().label + "\"?") != QMessageBox::Yes) return;
        theme::removeUser(n);
        emit themeChosen("warzone");
        rebuildThemeTiles();
    });
    v->addWidget(looks_);

    auto* scoring = new QGroupBox("Scoring & detection");
    auto* sg = new QGridLayout(scoring);
    sg->addWidget(label("How much each kind of site is trusted (0–100), and when a code counts as ready. Higher trust and more "
                        "sites listing a code raise its score.", "Muted"), 0, 0, 1, 4);
    const std::tuple<const char*, const char*, int, int> fields[] = {
        {"source_scoring.official", "Official sites", 0, 100},
        {"source_scoring.established_publisher", "Big publishers", 0, 100},
        {"source_scoring.community", "Community sites", 0, 100},
        {"source_scoring.unverified", "Unverified sites", 0, 100},
        {"intelligence.ready_confidence_threshold", "Ready at score", 0, 100},
        {"intelligence.corroboration_bonus_per_source", "Bonus per extra site", 0, 30},
        {"intelligence.corroboration_bonus_cap", "Bonus cap", 0, 60},
        {"intelligence.odd_shape_penalty", "Odd-looking code penalty", 0, 60},
        {"intelligence.codm_shape_bonus", "CODM-shaped code bonus", 0, 30},
        {"intelligence.min_code_length", "Shortest code", 4, 20},
        {"intelligence.max_code_length", "Longest code", 8, 40}};
    int srow = 1, col = 0;
    for (auto& [key, text, lo, hi] : fields) {
        auto* sp = new QSpinBox;
        sp->setRange(lo, hi);
        scoring_[key] = sp;
        sg->addWidget(new QLabel(text), srow, col * 2);
        sg->addWidget(sp, srow, col * 2 + 1);
        if (++col == 2) { col = 0; ++srow; }
    }
    if (col) ++srow;
    blocklist_ = new QPlainTextEdit;
    blocklist_->setPlaceholderText("One word per line — never treated as a code");
    blocklist_->setFixedHeight(90);
    sg->addWidget(new QLabel("Never a code"), srow, 0, Qt::AlignTop);
    sg->addWidget(blocklist_, srow, 1, 1, 3);
    auto* resetScoring = button("Reset to defaults", "ghost");
    sg->addWidget(resetScoring, srow + 1, 0, 1, 2, Qt::AlignLeft);
    connect(resetScoring, &QPushButton::clicked, this, [this] {
        Json def;
        try { def = Json::parse(readFile(ctx_.engine.config.paths().appDir / "config.json")); } catch (...) { return; }
        Json cfg = ctx_.engine.config.snapshot();
        cfg["source_scoring"] = def.get("source_scoring");
        for (const char* k : {"ready_confidence_threshold", "corroboration_bonus_per_source", "corroboration_bonus_cap", "odd_shape_penalty",
                              "codm_shape_bonus", "min_code_length", "max_code_length", "code_blocklist"})
            cfg["intelligence"][k] = def.get("intelligence").get(k);
        ctx_.engine.config.save(cfg);
        loadScoring();
        Toast::popup(ctx_.window, "Scoring reset to the defaults.");
    });
    v->addWidget(scoring);

    auto* lang = new QGroupBox("Language");
    auto* lgf = new QFormLayout(lang);
    language_ = new QComboBox;
    for (const auto& [code, name] : languageNames()) language_->addItem(q(name), q(code));
    langInfo_ = label("", "Muted");
    langInfo_->setWordWrap(true);
    auto* langRow = new QHBoxLayout;
    auto* applyLang = button("Apply & restart", "ghost");
    auto* aiLang = button("Translate the rest with AI", "ghost");
    langRow->addWidget(applyLang);
    langRow->addWidget(aiLang);
    langRow->addStretch(1);
    lgf->addRow("App language", language_);
    lgf->addRow(langInfo_);
    lgf->addRow(langRow);
    connect(applyLang, &QPushButton::clicked, this, [this] {
        ctx_.engine.config.writeEnv({{"APP_LANGUAGE", s(language_->currentData().toString())}});
        if (QMessageBox::question(this, "Restart now?", "The language changes after a restart. Restart the app now?") != QMessageBox::Yes) return;
        QStringList args = QApplication::arguments();
        args.removeFirst();
        QProcess::startDetached(QApplication::applicationFilePath(), args);
        QApplication::quit();
    });
    connect(aiLang, &QPushButton::clicked, this, [this] {
        QString code = language_->currentData().toString();
        if (code == "en") { Toast::popup(ctx_.window, "English needs no translation.", false); return; }
        if (ctx_.ai->busy()) { Toast::popup(ctx_.window, "Busy with another job — try again in a moment.", false); return; }
        ctx_.ai->setBusy(true);
        langInfo_->setText("Translating with your AI provider… this takes a minute or two.");
        Json a = Json::object();
        a["code"] = s(code);
        emit ctx_.ai->job("translate", q(a.dump()));
    });
    connect(language_, &QComboBox::currentIndexChanged, this, [this] { updateLanguageInfo(); });
    v->addWidget(lang);

    auto* push = new QGroupBox("Phone push (ntfy — free, no account)");
    auto* pf = new QFormLayout(push);
    pushInfo_ = label("", "Muted");
    pushInfo_->setWordWrap(true);
    pushInfo_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    pushInfo_->setOpenExternalLinks(true);
    pf->addRow(pushInfo_);
    pushCodes_ = new QCheckBox("New codes that are ready to redeem");
    pushPatch_ = new QCheckBox("Patch notes with weapon buffs / nerfs");
    pushCommunity_ = new QCheckBox("New builds from creators you follow");
    for (auto* c : {pushCodes_, pushPatch_, pushCommunity_}) pf->addRow(c);
    auto* pushRow = new QHBoxLayout;
    auto* makeTopic = button("Create my push topic", "ghost");
    auto* testPush = button("Send a test push", "ghost");
    auto* offPush = button("Turn off", "ghost");
    pushRow->addWidget(makeTopic);
    pushRow->addWidget(testPush);
    pushRow->addWidget(offPush);
    pushRow->addStretch(1);
    pf->addRow(pushRow);
    connect(makeTopic, &QPushButton::clicked, this, [this] {
        if (!ctx_.engine.config.env("NTFY_TOPIC").empty() &&
            QMessageBox::question(this, "New topic?", "You already have a push topic. Make a new one? Your phone will need to subscribe again.") != QMessageBox::Yes)
            return;
        ctx_.engine.config.writeEnv({{"NTFY_TOPIC", newPushTopic()}});
        refresh();
    });
    connect(offPush, &QPushButton::clicked, this, [this] { ctx_.engine.config.writeEnv({{"NTFY_TOPIC", ""}}); refresh(); });
    connect(testPush, &QPushButton::clicked, this, [this] {
        if (ctx_.engine.config.env("NTFY_TOPIC").empty()) { Toast::popup(ctx_.window, "Create your push topic first.", false); return; }
        save();
        emit ctx_.ai->job("push_test", "{}");
    });
    connect(ctx_.ai->worker(), &AiWorker::jobDone, this, [this](const QString& op, const QString& result, const QString& err) {
        if (op == "push_test") {
            Toast::popup(ctx_.window, err.isEmpty() ? QStringLiteral("Test push sent — check your phone.") : err, err.isEmpty(), 5000);
        } else if (op == "translate") {
            ctx_.ai->setBusy(false);
            if (!err.isEmpty()) { Toast::popup(ctx_.window, err, false, 6000); refresh(); return; }
            Json r;
            try { r = Json::parse(s(result)); } catch (...) {}
            Toast::popup(ctx_.window, QStringLiteral("%1 more text(s) translated — restart to see them.").arg(jinteger(r, "added")));
            refresh();
        }
    });
    connect(ctx_.ai->worker(), &AiWorker::researchLine, this, [this](const QString& l) {
        if (ctx_.ai->busy() && langInfo_->text().startsWith("Translating")) langInfo_->setText("Translating… " + l);
    });
    v->addWidget(push);

    auto* data = new QGroupBox("Your data");
    auto* df = new QVBoxLayout(data);
    dataPath_ = label("", "Muted");
    auto* open = button("Open data folder", "ghost");
    connect(open, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(q(ctx_.engine.config.paths().dataDir.string())));
    });
    df->addWidget(dataPath_);
    df->addWidget(open, 0, Qt::AlignLeft);
    v->addWidget(data);

    auto* saveBtn = button("Save settings", "accent");
    connect(saveBtn, &QPushButton::clicked, this, [this] { save(); Toast::popup(ctx_.window, "Settings saved."); refresh(); });
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(saveBtn);
    v->addLayout(row);
    v->addStretch(1);
    scroll->setWidget(inner);
    outer->addWidget(scroll);
}

void SettingsPage::refresh() {
    Config& c = ctx_.engine.config;
    uid_->setText(q(c.env("CODM_UID")));
    int r = region_->findText(q(c.env("ACCOUNT_REGION", "Both")), Qt::MatchFixedString);
    region_->setCurrentIndex(r < 0 ? 0 : r);
    urlGlobal_->setText(q(c.env("REDEMPTION_URL_GLOBAL")));
    urlGarena_->setText(q(c.env("REDEMPTION_URL_GARENA")));
    interval_->setValue(std::max(2, c.pollIntervalSeconds() / 60));
    onLaunch_->setChecked(c.runtime("start_monitoring_on_launch").toBool(true));
    autostart_->setChecked(autostart::enabled());
    tray_->setChecked(c.runtime("minimize_to_tray").toBool(true));
    notify_->setChecked(c.runtime("windows_notifications").toBool(true));
    awake_->setChecked(c.runtime("keep_awake").toBool(true));
    rebuildThemeTiles();
    loadScoring();
    auto ok = ctx_.engine.protection.checkAllowed();
    int used = ctx_.engine.protection.limiter.usedToday(), cap = ctx_.engine.protection.limiter.dailyCap();
    protection_->setText(QStringLiteral("%1 %2  ·  %3/%4 redemptions today")
                             .arg(ok.first ? QStringLiteral("●") : QStringLiteral("▲"), q(ok.second)).arg(used).arg(cap));
    dataPath_->setText(q(c.paths().dataDir.string()));

    {
        QSignalBlocker lb(language_);
        language_->setCurrentIndex(std::max(0, language_->findData(q(c.env("APP_LANGUAGE", "en")))));
    }
    updateLanguageInfo();

    Json cfg = c.snapshot();
    const Json ev = cfg.get("push").get("events");
    pushCodes_->setChecked(ev.get("codes").toBool(true));
    pushPatch_->setChecked(ev.get("patch").toBool(true));
    pushCommunity_->setChecked(ev.get("community").toBool(true));
    if (c.env("NTFY_TOPIC").empty())
        pushInfo_->setText("Get a push on your phone for new codes, patch changes and builds from creators you follow. "
                           "Install the free ntfy app (Android / iPhone), then press \"Create my push topic\".");
    else {
        QString url = q(pushSubscribeUrl(c));
        pushInfo_->setText(QStringLiteral("In the ntfy app, subscribe to <b>%1</b> on %2 — or open <a href='%3'>%3</a> on your phone. "
                                          "Keep the topic name private: anyone who has it can read your pushes.")
                               .arg(q(c.env("NTFY_TOPIC")).toHtmlEscaped(), q(pushServer(c)).toHtmlEscaped(), url.toHtmlEscaped()));
    }
}

void SettingsPage::loadScoring() {
    Json cfg = ctx_.engine.config.snapshot();
    for (auto it = scoring_.begin(); it != scoring_.end(); ++it) {
        const QStringList parts = it.key().split('.');
        QSignalBlocker b(it.value());
        it.value()->setValue(int(cfg.get(s(parts[0])).get(s(parts[1])).toInt(it.value()->value())));
    }
    QStringList words;
    for (const auto& w : cfg.get("intelligence").get("code_blocklist").elements()) words << q(w.toString());
    blocklist_->setPlainText(words.join('\n'));
}

void SettingsPage::rebuildThemeTiles() {
    auto* grid = static_cast<QGridLayout*>(tiles_->layout());
    while (auto* it = grid->takeAt(0)) {
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
    const QSize tile(176, 100);
    int i = 0;
    const QString cur = theme::current().name;
    for (const auto& n : theme::names()) {
        Palette p = theme::get(n);
        auto* b = new QPushButton;
        b->setProperty("kind", "tile");
        b->setCheckable(true);
        b->setChecked(n == cur);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(tile + QSize(8, 8));
        b->setIcon(QIcon(themePreview(p, tile, devicePixelRatioF())));
        b->setIconSize(tile);
        b->setToolTip(p.label + (p.builtin ? QString() : QStringLiteral("  (yours)")));
        connect(b, &QPushButton::clicked, this, [this, n] {
            emit themeChosen(n);
            rebuildThemeTiles();
        });
        grid->addWidget(b, i / 4, i % 4);
        ++i;
    }
    deleteTheme_->setEnabled(!theme::isBuiltin(cur));
}

void SettingsPage::showThemes() {
    if (scroll_ && looks_) scroll_->ensureWidgetVisible(looks_, 0, 40);
}

void SettingsPage::updateLanguageInfo() {
    Config& c = ctx_.engine.config;
    QString code = language_->currentData().toString();
    if (code == "en") langInfo_->setText("English is built in.");
    else {
        Translator t(c.paths());
        t.load(s(code));
        size_t missing = t.missing().size();
        langInfo_->setText(missing ? QStringLiteral("%1 text(s) aren't translated yet and show in English. \"Translate the rest with AI\" "
                                                    "fills them in with your AI provider (saved in your data folder).").arg(missing)
                                   : QStringLiteral("Fully translated."));
    }

}

void SettingsPage::save() {
    Config& c = ctx_.engine.config;
    QString g = urlGlobal_->text().trimmed(), ga = urlGarena_->text().trimmed();
    for (const QString& u : {g, ga})
        if (!u.isEmpty() && !u.startsWith("https://")) { Toast::popup(ctx_.window, "Redemption pages must start with https://", false); return; }
    c.writeEnv({{"CODM_UID", s(uid_->text().trimmed())}, {"ACCOUNT_REGION", s(region_->currentText())},
                {"REDEMPTION_URL_GLOBAL", s(g)}, {"REDEMPTION_URL_GARENA", s(ga)}});
    Json cfg = c.snapshot();
    cfg["discovery"]["poll_interval_seconds"] = interval_->value() * 60;
    Json& rt = cfg["runtime"];
    rt["start_monitoring_on_launch"] = onLaunch_->isChecked();
    rt["minimize_to_tray"] = tray_->isChecked();
    rt["windows_notifications"] = notify_->isChecked();
    rt["keep_awake"] = awake_->isChecked();
    rt["theme"] = s(theme::current().name);
    for (auto it = scoring_.begin(); it != scoring_.end(); ++it) {
        const QStringList parts = it.key().split('.');
        cfg[s(parts[0])][s(parts[1])] = it.value()->value();
    }
    if (cfg["intelligence"]["min_code_length"].toInt() > cfg["intelligence"]["max_code_length"].toInt())
        cfg["intelligence"]["max_code_length"] = cfg["intelligence"]["min_code_length"].toInt();
    Json block = Json::array();
    for (const auto& w : blocklist_->toPlainText().split('\n')) {
        QString t = w.trimmed().toUpper();
        if (!t.isEmpty() && block.size() < 500) block.push(s(t));
    }
    cfg["intelligence"]["code_blocklist"] = block;
    cfg["push"]["events"]["codes"] = pushCodes_->isChecked();
    cfg["push"]["events"]["patch"] = pushPatch_->isChecked();
    cfg["push"]["events"]["community"] = pushCommunity_->isChecked();
    c.save(cfg);
    if (autostart_->isChecked() != autostart::enabled()) {
        QString err;
        if (!autostart::setEnabled(autostart_->isChecked(), &err)) Toast::popup(ctx_.window, err, false);
    }
}
