#ifndef KAKUYOMUSERVICE_H
#define KAKUYOMUSERVICE_H

#include <QObject>
#include <QSet>
#include "novelwork.h"
#include "browserpagefetcher.h"
#include "httpclient.h"

// 业务层：编排“获取章节列表”与“下载章节”两个流程
class KakuyomuService : public QObject
{
    Q_OBJECT
public:
    explicit KakuyomuService(QObject *parent = nullptr);

    // 输入支持站点的作品网址，Pixiv 支持公开单篇和系列。
    // 解析出站点与书籍编号后抓取章节列表
    void fetchWork(const QString &workUrl);

    // 下载指定章节到 DOWNLOAD/<siteKey>/<workId>/chapters/，并初始化 novel.json
    void downloadChapters(const NovelWork &work, const QVector<Chapter> &chapters,
                          const QString &downloadRoot);

    // 中止当前下载任务
    void abort();

signals:
    void workReady(const NovelWork &work);
    void workFailed(const QString &error);
    void progressChanged(int current, int total, const QString &message);
    void downloadFinished(int successCount, int failedCount);

private slots:
    void onRequestFinished(const QString &url, const QVariant &context,
                           const QByteArray &data, bool ok, const QString &error);

private:
    void downloadNext();
    void retryCurrentChapter();
    void requestPage(const QString &url, const QVariant &context);

    HttpClient m_http;
    BrowserPageFetcher m_browser;
    QVector<Chapter> m_queue;
    QString m_workTitle;
    QString m_siteKey;
    QString m_chaptersDir;
    QString m_workUrl;
    NovelWork m_pendingWork;
    QSet<QString> m_cataloguePages;
    QSet<QString> m_episodeIds;
    quint64 m_generation = 0;
    int m_index = 0;
    int m_success = 0;
    int m_failed = 0;
    bool m_aborted = false;
    int m_retries = 0;
};

#endif // KAKUYOMUSERVICE_H

