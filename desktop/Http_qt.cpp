#include "Http.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace http
{
void Get(const QString& url, QObject* ctx, std::function<void(int, const QByteArray&)> done)
{
    static QNetworkAccessManager* net = new QNetworkAccessManager;
    QNetworkRequest rq{QUrl(url)};
    rq.setHeader(QNetworkRequest::UserAgentHeader, "DualHD/1.0");
    rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* r = net->get(rq);
    QObject::connect(r, &QNetworkReply::finished, ctx, [r, done] {
        r->deleteLater();
        int status = r->error() == QNetworkReply::NoError || r->error() >= QNetworkReply::ContentAccessDenied
            ? r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
        done(status, r->readAll());
    });
}
}
