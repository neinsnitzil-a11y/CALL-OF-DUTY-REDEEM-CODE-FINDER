#include "uitranslate.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QWidget>
#include <QWizardPage>
#include <memory>

namespace {
std::unique_ptr<hunter::Translator> g_tr;

class Filter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show || e->type() == QEvent::Polish)
            if (auto* w = qobject_cast<QWidget*>(o)) ui18n::translateTree(w);
        return false;
    }
};

bool isWordChar(QChar c) { return c.isLetter() || c.isDigit(); }

void once(QWidget* w, const char* key, const std::function<void()>& fn) {
    if (w->property(key).toBool()) return;
    w->setProperty(key, true);
    fn();
}
}  // namespace

namespace ui18n {

hunter::Translator* translator() { return g_tr.get(); }

QString tr(const QString& english) {
    if (!g_tr || g_tr->code() == "en" || english.isEmpty()) return english;
    std::string exact = g_tr->t(english.toStdString());
    if (exact != english.toStdString()) return QString::fromStdString(exact);
    // keep the leading emoji / spaces and trailing marks, translate the words in between
    int a = 0, b = int(english.size());
    while (a < b && !isWordChar(english[a])) ++a;
    while (b > a && !isWordChar(english[b - 1]) && english[b - 1] != QChar('?') && english[b - 1] != QChar('.')) --b;
    if (a >= b) return english;
    QString core = english.mid(a, b - a);
    std::string t = g_tr->t(core.toStdString());
    if (t == core.toStdString()) {
        // "Scanning…" / "Redeem…" → try without the trailing dots too
        QString bare = core;
        while (!bare.isEmpty() && (bare.endsWith('.') || bare.endsWith(QChar(0x2026)))) bare.chop(1);
        t = g_tr->t(bare.toStdString());
        if (t == bare.toStdString()) return english;
        return english.left(a) + QString::fromStdString(t) + english.mid(a + int(bare.size()));
    }
    return english.left(a) + QString::fromStdString(t) + english.mid(b);
}

void translateTree(QWidget* root) {
    if (!g_tr || g_tr->code() == "en" || !root) return;
    QList<QWidget*> all = root->findChildren<QWidget*>();
    all.prepend(root);
    for (QWidget* w : all) {
        once(w, "_i18n", [w] {
            if (!w->toolTip().isEmpty()) w->setToolTip(tr(w->toolTip()));
            if (w->isWindow() && !w->windowTitle().isEmpty()) w->setWindowTitle(tr(w->windowTitle()));
            if (auto* l = qobject_cast<QLabel*>(w)) { if (!l->text().isEmpty() && l->textFormat() != Qt::RichText) l->setText(tr(l->text())); }
            else if (auto* b = qobject_cast<QAbstractButton*>(w)) b->setText(tr(b->text()));
            else if (auto* g = qobject_cast<QGroupBox*>(w)) g->setTitle(tr(g->title()));
            else if (auto* e = qobject_cast<QLineEdit*>(w)) e->setPlaceholderText(tr(e->placeholderText()));
            else if (auto* p = qobject_cast<QPlainTextEdit*>(w)) p->setPlaceholderText(tr(p->placeholderText()));
            else if (auto* t = qobject_cast<QTextEdit*>(w)) t->setPlaceholderText(tr(t->placeholderText()));
            else if (auto* wp = qobject_cast<QWizardPage*>(w)) { wp->setTitle(tr(wp->title())); wp->setSubTitle(tr(wp->subTitle())); }
        });
        // lists / tabs / tables / combos can gain items later: translate the new ones each time
        if (auto* t = qobject_cast<QTabWidget*>(w))
            for (int i = 0; i < t->count(); ++i) t->setTabText(i, tr(t->tabText(i)));
        if (auto* c = qobject_cast<QComboBox*>(w))
            for (int i = 0; i < c->count(); ++i) {
                if (c->itemData(i, Qt::UserRole + 50).toBool()) continue;
                c->setItemText(i, tr(c->itemText(i)));
                c->setItemData(i, true, Qt::UserRole + 50);
            }
        if (auto* lw = qobject_cast<QListWidget*>(w))
            for (int i = 0; i < lw->count(); ++i) {
                auto* it = lw->item(i);
                if (it->data(Qt::UserRole + 50).toBool()) continue;
                it->setText(tr(it->text()));
                if (it->data(Qt::UserRole + 1).isValid()) it->setData(Qt::UserRole + 1, tr(it->data(Qt::UserRole + 1).toString()));
                it->setData(Qt::UserRole + 50, true);
            }
        if (auto* tw = qobject_cast<QTableWidget*>(w))
            for (int i = 0; i < tw->columnCount(); ++i)
                if (auto* h = tw->horizontalHeaderItem(i); h && !h->data(Qt::UserRole + 50).toBool()) {
                    h->setText(tr(h->text()));
                    h->setData(Qt::UserRole + 50, true);
                }
    }
}

void install(const hunter::Paths& paths, const std::string& code) {
    g_tr = std::make_unique<hunter::Translator>(paths);
    g_tr->load(code);
    if (code == "en") return;
    qApp->installEventFilter(new Filter(qApp));
    if (g_tr->rightToLeft()) qApp->setLayoutDirection(Qt::RightToLeft);
}

}  // namespace ui18n
