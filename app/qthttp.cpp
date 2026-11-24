#include "qthttp.hpp"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThreadStorage>
#include <QTimer>
#include <QUrl>

namespace {
// One manager per thread: QNetworkAccessManager must live on the thread that uses it.
QNetworkAccessManager& manager() {
    static QThreadStorage<QNetworkAccessManager*> store;
    if (!store.hasLocalData()) store.setLocalData(new QNetworkAccessManager());
    return *store.localData();
}
}  // namespace

namespace {
QNetworkReply* start(const hunter::HttpRequest& req) {
    QNetworkRequest r(QUrl(QString::fromStdString(req.url)));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setMaximumRedirectsAllowed(8);
    r.setTransferTimeout(req.timeoutSeconds * 1000);
    r.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,application/json;q=0.8,*/*;q=0.5");
    r.setRawHeader("Accept-Language", "en-US,en;q=0.8");
    for (const auto& [k, v] : req.headers) r.setRawHeader(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
    if (!req.contentType.empty()) r.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromStdString(req.contentType));
    const QByteArray body = QByteArray::fromStdString(req.body);
    if (req.method == "GET") return manager().get(r);
    if (req.method == "POST") return manager().post(r, body);
    return manager().sendCustomRequest(r, QByteArray::fromStdString(req.method), body);
}

void finish(QNetworkReply* reply, hunter::HttpResponse& out) {
    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.finalUrl = reply->url().toString().toStdString();
    for (const auto& pair : reply->rawHeaderPairs())
        out.headers[pair.first.toLower().toStdString()] = pair.second.toStdString();
    if (reply->error() != QNetworkReply::NoError && out.status == 0) out.error = reply->errorString().toStdString();
}
}  // namespace

hunter::HttpResponse QtHttpClient::send(const hunter::HttpRequest& req) {
    hunter::HttpResponse out;
    QNetworkReply* reply = start(req);
    QEventLoop loop;
    QTimer guard;  // belt and braces on top of the transfer timeout
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    guard.start((req.timeoutSeconds + 10) * 1000);
    loop.exec();
    out.body = reply->readAll().toStdString();
    finish(reply, out);
    reply->deleteLater();
    return out;
}

hunter::HttpResponse QtHttpClient::stream(const hunter::HttpRequest& req, const std::function<bool(const std::string&)>& onData) {
    hunter::HttpResponse out;
    QNetworkReply* reply = start(req);
    QEventLoop loop;
    bool cancelled = false;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray chunk = reply->readAll();
        if (status >= 400) { out.body += chunk.toStdString(); return; }  // keep error text for the message
        if (!cancelled && !onData(chunk.toStdString())) {
            cancelled = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray rest = reply->readAll();
    if (!rest.isEmpty()) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400) out.body += rest.toStdString();
        else if (!cancelled) onData(rest.toStdString());
    }
    finish(reply, out);
    if (cancelled) out.error.clear();
    reply->deleteLater();
    return out;
}
