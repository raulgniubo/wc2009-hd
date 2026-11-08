#include "Http.h"
#include <QPointer>
#include <QMetaObject>
#import <Foundation/Foundation.h>

namespace http
{
void Get(const QString& url, QObject* ctx, std::function<void(int, const QByteArray&)> done)
{
    QPointer<QObject> guard(ctx);
    NSURL* u = [NSURL URLWithString:url.toNSString()];
    if (!u) { QMetaObject::invokeMethod(ctx, [done] { done(0, QByteArray()); }, Qt::QueuedConnection); return; }
    NSMutableURLRequest* rq = [NSMutableURLRequest requestWithURL:u cachePolicy:NSURLRequestUseProtocolCachePolicy timeoutInterval:20];
    [rq setValue:@"DualHD/1.0" forHTTPHeaderField:@"User-Agent"];
    [[NSURLSession.sharedSession dataTaskWithRequest:rq completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
        int status = (!err && [resp isKindOfClass:NSHTTPURLResponse.class]) ? (int)((NSHTTPURLResponse*)resp).statusCode : 0;
        QByteArray body = data ? QByteArray((const char*)data.bytes, (qsizetype)data.length) : QByteArray();
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, done, status, body] { if (guard) done(status, body); }, Qt::QueuedConnection);
    }] resume];
}
}
