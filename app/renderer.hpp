// Qt WebEngine helpers (only built with HUNTER_WITH_WEBENGINE):
//  * PageRenderer: draws JavaScript-built code pages for the scanner. The scanner thread calls
//    renderBlocking(); the page itself is loaded on the GUI thread (WebEngine requires it).
//  * checkOfficialPage(): loads a redemption page invisibly and reports whether the UID / code boxes
//    are where the app expects them. Never types or submits anything.
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <string>

class QWebEngineProfile;

class PageRenderer : public QObject {
    Q_OBJECT
public:
    explicit PageRenderer(const QString& userAgent, QObject* parent = nullptr);
    // Thread-safe; blocks the calling (non-GUI) thread. Throws std::runtime_error on failure/timeout.
    std::string renderBlocking(const std::string& url, int timeoutMs = 25000);
    QWebEngineProfile* profile() const { return profile_; }

private:
    QWebEngineProfile* profile_;
};

struct PageCheck {
    bool loaded = false;
    int uidBoxes = 0, codeBoxes = 0, captchas = 0;
    QString title, error;
};

void checkOfficialPage(QWebEngineProfile* profile, const QString& url, const QStringList& uidSelectors,
                       const QStringList& codeSelectors, QObject* context, std::function<void(const PageCheck&)> done);
