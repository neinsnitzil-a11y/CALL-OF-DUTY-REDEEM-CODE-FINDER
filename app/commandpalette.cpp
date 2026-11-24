#include "commandpalette.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include "art.hpp"
#include "theme.hpp"
#include "uitranslate.hpp"

CommandPalette::CommandPalette(QWidget* parent) : QDialog(parent, Qt::FramelessWindowHint | Qt::Popup) {
    setObjectName("Palette");
    setAttribute(Qt::WA_StyledBackground);
    resize(620, 420);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 8);
    v->setSpacing(4);
    input_ = new QLineEdit;
    input_->setPlaceholderText(ui18n::tr("Type a page, an action, a theme — or a code to search for"));
    input_->addAction(art::icon("search", theme::current().muted), QLineEdit::LeadingPosition);
    list_ = new QListWidget;
    list_->setIconSize(QSize(18, 18));
    list_->setUniformItemSizes(true);
    v->addWidget(input_);
    v->addWidget(list_, 1);
    input_->installEventFilter(this);
    connect(input_, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(list_, &QListWidget::itemActivated, this, [this] { runCurrent(); });
    connect(list_, &QListWidget::itemClicked, this, [this] { runCurrent(); });
}

void CommandPalette::openWith(const QString& text) {
    if (auto* w = parentWidget()) {
        QPoint c = w->mapToGlobal(QPoint(w->width() / 2, 0));
        move(c.x() - width() / 2, c.y() + 70);
    }
    input_->setText(text);
    filter();
    show();
    raise();
    activateWindow();
    input_->setFocus();
}

void CommandPalette::filter() {
    const QString q = input_->text().trimmed();
    const QStringList words = q.toLower().split(' ', Qt::SkipEmptyParts);
    list_->clear();
    shown_.clear();
    const Palette& p = theme::current();
    for (int i = 0; i < int(cmds_.size()); ++i) {
        const auto& c = cmds_[size_t(i)];
        const QString hay = (ui18n::tr(c.title) + " " + c.title + " " + c.hint).toLower();
        bool ok = true;
        for (const auto& w : words) if (!hay.contains(w)) { ok = false; break; }
        if (!ok) continue;
        auto* it = new QListWidgetItem(art::icon(c.icon, p.accent), ui18n::tr(c.title) + (c.hint.isEmpty() ? QString() : "   ·   " + c.hint));
        list_->addItem(it);
        shown_.push_back(i);
        if (shown_.size() >= 60) break;
    }
    if (!q.isEmpty() && search_) {
        auto* it = new QListWidgetItem(art::icon("search", p.muted), ui18n::tr("Search codes for") + " “" + q + "”");
        list_->addItem(it);
        shown_.push_back(-1);
    }
    if (list_->count()) list_->setCurrentRow(0);
}

void CommandPalette::runCurrent() {
    int r = list_->currentRow();
    if (r < 0 || r >= int(shown_.size())) return;
    int idx = shown_[size_t(r)];
    const QString text = input_->text().trimmed();
    hide();
    if (idx < 0) { if (search_) search_(text); return; }
    if (cmds_[size_t(idx)].run) cmds_[size_t(idx)].run();
}

bool CommandPalette::eventFilter(QObject* o, QEvent* e) {
    if (o == input_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        const int n = list_->count();
        if (k->key() == Qt::Key_Down && n) { list_->setCurrentRow((list_->currentRow() + 1) % n); return true; }
        if (k->key() == Qt::Key_Up && n) { list_->setCurrentRow((list_->currentRow() - 1 + n) % n); return true; }
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { runCurrent(); return true; }
        if (k->key() == Qt::Key_Escape) { hide(); return true; }
    }
    return QDialog::eventFilter(o, e);
}
