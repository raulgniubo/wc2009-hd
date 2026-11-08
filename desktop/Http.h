// Small HTTPS GET with the system's own networking (NSURLSession on macOS, WinHTTP on Windows,
// Qt elsewhere), so downloads work without bundling a TLS library into the static Qt build.
#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>

namespace http
{
// done(status, body) runs on ctx's thread (the GUI thread), and not at all if ctx is gone by then.
// status is the HTTP status code, or 0 when the request couldn't be made.
void Get(const QString& url, QObject* ctx, std::function<void(int status, const QByteArray& body)> done);
}
