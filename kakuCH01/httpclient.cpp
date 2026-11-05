#include "httpclient.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QPointer>
#include <QUrl>

namespace {
constexpr char kUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36";
}

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
{
}

void HttpClient::attach(QNetworkReply *reply, const QString &url, const QVariant &context)
{
    // 把请求来源信息挂到 reply 上，异步回调时据此区分不同请求
    reply->setProperty("url", url);
    reply->setProperty("context", context);
    if (m_transferTimeoutMs > 0)
        attachTimeout(reply);
    m_replies.append(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_replies.removeAll(reply);
        QString url = reply->property("url").toString();
        QVariant context = reply->property("context");
        bool ok = (reply->error() == QNetworkReply::NoError);
        QByteArray data = reply->readAll();
        QString error = ok ? QString() : reply->errorString();
        // llama-server places the actionable cause of HTTP 4xx errors in the
        // response body. Preserve a bounded excerpt instead of reporting only
        // the unhelpful generic "Bad Request" string.
        if (!ok) {
            const QString detail = QString::fromUtf8(data).trimmed();
            if (!detail.isEmpty())
                error += QStringLiteral(": ") + detail.left(600);
        }
        reply->deleteLater();
        emit requestFinished(url, context, data, ok, error);
    });
}

void HttpClient::get(const QString &url, const QVariant &context)
{
    QNetworkRequest request((QUrl(url)));
    // 伪装浏览器请求头，避免被目标站点拒绝
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    const QUrl target(url);
    if (target.host() == "www.pixiv.net" || target.host() == "pixiv.net") {
        request.setRawHeader("Referer", "https://www.pixiv.net/");
        if (target.path().startsWith("/ajax/")) request.setRawHeader("Accept", "application/json");
    }
    if (!m_cookieHeader.isEmpty())
        request.setRawHeader("Cookie", m_cookieHeader.toUtf8());
    attach(m_manager.get(request), url, context);
}

void HttpClient::setCookieHeader(const QString &cookie)
{
    m_cookieHeader = cookie;
}

void HttpClient::postJson(const QString &url, const QByteArray &json, const QVariant &context)
{
    QNetworkRequest request((QUrl(url)));
    request.setRawHeader("User-Agent", kUserAgent);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    attach(m_manager.post(request, json), url, context);
}

void HttpClient::postForm(const QString &url, const QByteArray &form, const QVariant &context)
{
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    const QUrl target(url);
    const QByteArray origin = (target.scheme() + "://" + target.authority()).toUtf8();
    request.setRawHeader("Origin", origin);
    request.setRawHeader("Referer", origin + '/');
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // QNetworkAccessManager keeps the GET session and CSRF cookies in memory.
    attach(m_manager.post(request, form), url, context);
}

void HttpClient::abortAll()
{
    // 中止所有在途请求；finished 回调会依次触发并清理列表
    const QList<QNetworkReply *> replies = m_replies;
    m_replies.clear();
    for (QNetworkReply *reply : replies)
        reply->abort();
}

void HttpClient::attachTimeout(QNetworkReply *reply)
{
    // 数据往来会重置看门狗：只要连接还活着就不算超时；长时间毫无数据才中止。
    QPointer<QNetworkReply> guard(reply);
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->setInterval(m_transferTimeoutMs);
    connect(timer, &QTimer::timeout, reply, [guard]() {
        if (guard && guard->isRunning())
            guard->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, timer, [timer]() { timer->start(); });
    timer->start();
}
