// Redemption window. Opens the official page with your UID and the code filled in; YOU solve the
// CAPTCHA and press Redeem. It never submits on its own. With Qt WebEngine the page opens inside
// the app and the result text is read back; without it, the code is copied and your browser opens.
#pragma once
#include <QDialog>
#include <QString>

#include "../core/engine.hpp"

class QLabel;
class QTimer;
#ifdef HUNTER_WITH_WEBENGINE
class QWebEngineView;
#endif

class RedeemWindow : public QDialog {
    Q_OBJECT
public:
    // Returns nullptr (and explains why in `why`) when protection rules say no.
    static RedeemWindow* open(hunter::Engine& engine, long long codeId, QWidget* parent, QString& why);

signals:
    void resultRecorded(long long codeId, const QString& outcome);

private:
    RedeemWindow(hunter::Engine& engine, const hunter::CodeRow& row, const QString& url, QWidget* parent);
    void record(const QString& outcome);   // SUCCESS / EXPIRED / INVALID / ALREADY_REDEEMED / LIMIT_REACHED / UNKNOWN
    void fillPage();
    void pollResult();
    void closeEvent(QCloseEvent* e) override;
    void reject() override { close(); }  // Esc goes through closeEvent too

public:
    ~RedeemWindow() override;

private:

    hunter::Engine& engine_;
    hunter::CodeRow row_;
    QString url_;
    long long attemptId_ = 0;
    bool usesUid_ = false, done_ = false, locked_ = false;
    QLabel* hint_ = nullptr;
    QLabel* detected_ = nullptr;
    QString detectedOutcome_;
#ifdef HUNTER_WITH_WEBENGINE
    QWebEngineView* view_ = nullptr;
    QTimer* poll_ = nullptr;
#endif
};
