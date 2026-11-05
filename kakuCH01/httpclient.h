#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QVariant>
#include <QList>
#include <QNetworkAccessManager>

class QNetworkReply;

// 网络层：封装 QNetworkAccessManager 的异步 GET / POST(JSON)
class HttpClient : public QObject
{
    Q_OBJECT
public:
    explicit HttpClient(QObject *parent = nullptr);

    // 发起异步 GET，context 用于标识请求来源
    void get(const QString &url, const QVariant &context = QVariant());

    // 发起异步 POST（application/json），body 为原始 JSON 字节
    void postJson(const QString &url, const QByteArray &json, const QVariant &context = QVariant());
    // HTML form submission. Redirects are returned to the caller for an explicit GET.
    void postForm(const QString &url, const QByteArray &form, const QVariant &context = QVariant());

    // 部分站点（如 alphapolis）只要请求带上任意非空 Cookie 头就会返回完整正文，
    // 不带则返回被裁剪的页面。为空时不覆盖 Qt 的内存会话 Cookie。
    void setCookieHeader(const QString &cookie);
    QString cookieHeader() const { return m_cookieHeader; }

    // 中止所有在途请求
    void abortAll();

    // 传输超时（毫秒）：请求在无任何数据往来的情况下超过该时长会以
    // TimeoutError 结束，交给上层重试。<= 0 表示不设超时。
    void setTransferTimeout(int ms) { m_transferTimeoutMs = ms; }
    int transferTimeout() const { return m_transferTimeoutMs; }

signals:
    void requestFinished(const QString &url, const QVariant &context,
                         const QByteArray &data, bool ok, const QString &errorString);

private:
    // 为 reply 挂载来源信息并连接回调
    void attach(QNetworkReply *reply, const QString &url, const QVariant &context);
    // 为 reply 挂载看门狗定时器（仅当设置了传输超时）
    void attachTimeout(QNetworkReply *reply);

    QNetworkAccessManager m_manager;
    QList<QNetworkReply *> m_replies; // 在途请求列表
    QString m_cookieHeader;
    int m_transferTimeoutMs = 0;
};

#endif // HTTPCLIENT_H
