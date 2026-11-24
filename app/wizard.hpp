// First-run setup: account, Discord, start with Windows. About a minute.
#pragma once
#include <QWizard>

#include "../core/engine.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;

class SetupWizard : public QWizard {
    Q_OBJECT
public:
    SetupWizard(hunter::Engine& engine, QWidget* parent = nullptr);
    void accept() override;
    bool startScanning() const;

private:
    hunter::Engine& engine_;
    QLineEdit *uid_, *webhook_;
    QComboBox* region_;
    QCheckBox *autostart_, *scanNow_;
};
