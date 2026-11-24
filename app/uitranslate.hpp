// Window translation: every label, button, tab, list item, placeholder and tooltip is translated when it
// first appears, from data/i18n/<language>.json (English → your language). Leading emoji / symbols and
// trailing marks ("▶  Start scanning", "Send ➤") are kept; only the words are looked up.
#pragma once
#include <QObject>
#include <QString>

#include "../core/i18n.hpp"

namespace ui18n {
void install(const hunter::Paths& paths, const std::string& code);  // call after the QApplication exists
QString tr(const QString& english);
hunter::Translator* translator();
void translateTree(QWidget* root);
}
