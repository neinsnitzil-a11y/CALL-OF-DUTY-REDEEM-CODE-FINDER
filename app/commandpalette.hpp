// Ctrl+K: type to jump to any page, run an action, switch theme or search codes.
#pragma once
#include <QDialog>
#include <QIcon>
#include <QString>
#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    struct Command {
        QString icon, title, hint;
        std::function<void()> run;
    };
    explicit CommandPalette(QWidget* parent = nullptr);
    void setCommands(std::vector<Command> cmds) { cmds_ = std::move(cmds); }
    void setSearchFallback(std::function<void(const QString&)> fn) { search_ = std::move(fn); }
    void openWith(const QString& text = QString());

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void filter();
    void runCurrent();
    QLineEdit* input_;
    QListWidget* list_;
    std::vector<Command> cmds_;
    std::vector<int> shown_;  // list row → command index (-1 = search codes)
    std::function<void(const QString&)> search_;
};
