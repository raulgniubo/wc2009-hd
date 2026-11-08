#include "Http.h"
#include <QPointer>
#include <QMetaObject>
#include <thread>
#include <windows.h>
#include <winhttp.h>

namespace http
{
static int Fetch(const std::wstring& url, QByteArray& body)
{
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return 0;
    int status = 0;
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
    // the system's proxy settings (Windows 8.1+), else the plain default
    HINTERNET s = WinHttpOpen(L"DualHD/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"DualHD/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return 0;
    // TLS 1.2+ even on older Windows 10 builds where it isn't the default
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))
    {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    WinHttpSetTimeouts(s, 8000, 8000, 15000, 20000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr))
    {
        DWORD code = 0, len = sizeof(code);
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX))
            status = (int)code;
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0) break;
            qsizetype at = body.size();
            body.resize(at + avail);
            DWORD got = 0;
            if (!WinHttpReadData(r, body.data() + at, avail, &got)) { body.resize(at); break; }
            body.resize(at + got);
            if (body.size() > 32 * 1024 * 1024) break;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return status;
}

void Get(const QString& url, QObject* ctx, std::function<void(int, const QByteArray&)> done)
{
    QPointer<QObject> guard(ctx);
    std::wstring u = url.toStdWString();
    std::thread([guard, done, u] {
        QByteArray body;
        int status = Fetch(u, body);
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, done, status, body] { if (guard) done(status, body); }, Qt::QueuedConnection);
    }).detach();
}
}
