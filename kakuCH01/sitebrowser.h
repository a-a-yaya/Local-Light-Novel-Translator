#ifndef SITEBROWSER_H
#define SITEBROWSER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include "novelwork.h"
#include "httpclient.h"

// 「网站」页的后端：在软件内检索/浏览站点内容（搜索、排行榜、分类、评论区）。
// 网页读取结果只保存于本次应用会话缓存；下载交给 KakuyomuService。
struct SiteSession;
class SiteBrowser : public QObject
{
    Q_OBJECT
public:
    explicit SiteBrowser(QObject *parent = nullptr);
    ~SiteBrowser() override;
    bool setSaveRoot(const QString &root);

    void openWork(const QString &url);
    void searchKakuyomu(const QString &keyword, int page, const QString &order = QStringLiteral("weekly_ranking"));
    void fetchKakuyomuRanking(const QString &genre, const QString &period, int page = 1);
    void invalidateCards();
    void clearSessionCache();
    void fetchKakuyomuComments(const QString &workId);
    void searchKakuyomuAuthorWorks(const QString &keyword);
    void abort();

signals:
    void workOpened(const NovelWork &work);
    void workStatsReady(const QVariantMap &stats);
    void workOpenFailed(const QString &error);
    void cardsReady(const QVariantList &cards);
    void searchPageReady(int totalPages);
    void cardsFailed(const QString &error);
    void commentsReady(const QVariantList &comments);
    void commentsFailed(const QString &error);
    void authorResolved(const QString &authorName);

private slots:
    void onRequestFinished(const QString &url, const QVariant &context,
                           const QByteArray &data, bool ok, const QString &error);

private:
    HttpClient m_http;
    std::unique_ptr<SiteSession> m_session;
    QString m_cacheRoot;
    QString m_cacheDirectory;
    quint64 m_cacheGeneration = 0;
    quint64 m_workRevision = 0;
    quint64 m_commentsRevision = 0;
    quint64 m_cardsRevision = 0;
    void requestPage(const QString &url, const QVariant &context);
    QString cachePath(const QString &url) const;
    void cachePage(const QString &url, const QByteArray &data);
};

#endif // SITEBROWSER_H
