#include "sitebrowser.h"
#include "novelparser.h"
#include "novelpaths.h"

#include <QUrl>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QLockFile>
#include <QSaveFile>
#include <QTimer>
#include <QFile>
#include <memory>

struct SiteSession {
    std::unique_ptr<QTemporaryDir> directory;
    std::unique_ptr<QLockFile> lock;
    explicit SiteSession(const QString &root) {
        if (root.isEmpty() || !QDir().mkpath(root)) return;
        QDir base(root);
        for (const QString &name : base.entryList({QStringLiteral("site-session-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString path = base.filePath(name);
            QLockFile oldLock(path + QStringLiteral("/session.lock"));
            if (oldLock.tryLock(0)) {
                oldLock.unlock();
                QDir(path).removeRecursively();
            }
        }
        directory = std::make_unique<QTemporaryDir>(base.filePath(QStringLiteral("site-session-XXXXXX")));
        if (directory->isValid()) {
            lock = std::make_unique<QLockFile>(directory->path() + QStringLiteral("/session.lock"));
            if (!lock->tryLock(0)) {
                lock.reset();
                directory.reset();
            }
        }
    }
};


SiteBrowser::SiteBrowser(QObject *parent)
    : QObject(parent)
{
    connect(&m_http, &HttpClient::requestFinished,
            this, &SiteBrowser::onRequestFinished);
}

SiteBrowser::~SiteBrowser() = default;

bool SiteBrowser::setSaveRoot(const QString &root)
{
    const QString cacheRoot = root.trimmed().isEmpty() ? QString()
        : QDir(QDir(root).absolutePath()).filePath(QStringLiteral("CACHE"));
    if (m_cacheRoot == cacheRoot) return false;
    if (!m_cacheRoot.isEmpty()) abort();
    m_session.reset(); // Unlock and remove the previous root's temporary session.
    m_cacheDirectory.clear();
    m_cacheRoot = cacheRoot;
    if (!cacheRoot.isEmpty()) {
        m_session = std::make_unique<SiteSession>(cacheRoot);
        if (m_session->directory && m_session->directory->isValid())
            m_cacheDirectory = m_session->directory->path();
    }
    return true;
}

void SiteBrowser::abort()
{
    invalidateCards();
    ++m_workRevision;
    ++m_commentsRevision;
    ++m_cacheGeneration;
    m_http.abortAll();
}

void SiteBrowser::invalidateCards()
{
    ++m_cardsRevision;
}

QString SiteBrowser::cachePath(const QString &url) const
{
    if (m_cacheDirectory.isEmpty()) return {};
    return QDir(m_cacheDirectory).filePath(QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".html");
}

void SiteBrowser::cachePage(const QString &url, const QByteArray &data)
{
    const QString path = cachePath(url);
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
}

void SiteBrowser::requestPage(const QString &url, const QVariant &context)
{
    QVariantMap ctx = context.toMap();
    if (ctx.isEmpty()) ctx.insert("kind", context.toString());
    ctx.insert("generation", m_cacheGeneration);
    if (ctx.value("kind") == "work") ctx.insert("workRevision", m_workRevision);
    if (ctx.value("kind") == "comments") ctx.insert("commentsRevision", m_commentsRevision);
    QFile file(cachePath(url));
    if (!file.fileName().isEmpty() && file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        if (!data.isEmpty()) {
            ctx.insert("cacheHit", true);
            QTimer::singleShot(0, this, [this, url, ctx, data]() { onRequestFinished(url, ctx, data, true, {}); });
            return;
        }
    }
    m_http.get(url, ctx);
}

void SiteBrowser::clearSessionCache()
{
    abort(); // Old replies/timers cannot recreate cleared cache or replace fresh results.
    if (m_cacheDirectory.isEmpty()) return;
    QDir dir(m_cacheDirectory);
    for (const QString &name : dir.entryList({QStringLiteral("*.html")}, QDir::Files))
        dir.remove(name);
}

void SiteBrowser::openWork(const QString &url)
{
    QString error;
    const QString workId = NovelPaths::workIdFromUrl(url, &error);
    if (workId.isEmpty()) {
        emit workOpenFailed(error);
        return;
    }
    const QString siteKey = NovelPaths::siteKeyFromUrl(url);
    if (!NovelParser::supportsParsing(siteKey)) {
        emit workOpenFailed(QStringLiteral("%1（%2）暂不支持，目前可用：カクヨム、ノベルアップ＋、アルファポリス")
                                .arg(NovelPaths::siteDisplayName(siteKey), siteKey));
        return;
    }
    ++m_workRevision;
    ++m_commentsRevision;
    m_http.setCookieHeader(siteKey == QStringLiteral("alphapoliscopjp")
                               ? QStringLiteral("alphapolis_session=1")
                               : QString());
    requestPage(NovelPaths::canonicalWorkUrl(url), QStringLiteral("work"));
}

// 搜索：kakuyomu 的搜索页是服务端渲染的，直接抓结果卡片
void SiteBrowser::searchKakuyomu(const QString &keyword, int page, const QString &order)
{
    invalidateCards();
    const QString kw = keyword.trimmed();
    if (kw.isEmpty()) {
        emit cardsFailed(QStringLiteral("请输入搜索关键词"));
        return;
    }
    m_http.setCookieHeader(QString());
    QUrl url(QStringLiteral("https://kakuyomu.jp/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), kw);
    const QStringList orders = {"popular", "weekly_ranking", "published_at", "last_episode_published_at"};
    q.addQueryItem(QStringLiteral("order"), orders.contains(order) ? order : QStringLiteral("weekly_ranking"));
    if (page > 1)
        q.addQueryItem(QStringLiteral("page"), QString::number(page));
    url.setQuery(q);
    requestPage(url.toString(), QVariantMap{{"kind", "cards"}, {"revision", m_cardsRevision}});
}

void SiteBrowser::searchKakuyomuAuthorWorks(const QString &keyword)
{
    invalidateCards();
    const QString kw = keyword.trimmed();
    if (kw.isEmpty()) {
        emit cardsFailed(QStringLiteral("请输入作者 ID 或笔名"));
        return;
    }
    m_http.setCookieHeader(QString());
    QUrl url(QStringLiteral("https://kakuyomu.jp/search_user"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), kw);
    url.setQuery(query);
    requestPage(url.toString(), QVariantMap{
        {"kind", "authorusers"}, {"revision", m_cardsRevision}, {"query", kw}
    });
}

// 排行榜 / 分类：/rankings/<genre>/weekly（genre 见 supportedGenres）
void SiteBrowser::fetchKakuyomuRanking(const QString &genre, const QString &period, int page)
{
    invalidateCards();
    const QString g = genre.trimmed().isEmpty() ? QStringLiteral("all") : genre.trimmed();
    const QString p = period.trimmed().isEmpty() ? QStringLiteral("weekly") : period.trimmed();
    m_http.setCookieHeader(QString());
    QUrl url(QStringLiteral("https://kakuyomu.jp/rankings/%1/%2").arg(g, p));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(qBound(1, page, 5)));
    url.setQuery(query);
    requestPage(url.toString(), QVariantMap{{"kind", "rankcards"}, {"revision", m_cardsRevision}});
}

void SiteBrowser::fetchKakuyomuComments(const QString &workId)
{
    if (workId.trimmed().isEmpty()) {
        emit commentsFailed(QStringLiteral("请先打开一本作品"));
        return;
    }
    ++m_commentsRevision;
    m_http.setCookieHeader(QString());
    requestPage(QStringLiteral("https://kakuyomu.jp/works/%1/reviews"
                              "?work_review_order=like_desc").arg(workId.trimmed()),
               QStringLiteral("comments"));
}

void SiteBrowser::onRequestFinished(const QString &url, const QVariant &context,
                                    const QByteArray &data, bool ok,
                                    const QString &error)
{
    const auto listContext = context.toMap();
    const QString kind = listContext.isEmpty() ? context.toString() : listContext.value("kind").toString();
    if (listContext.contains("generation") && listContext.value("generation").toULongLong() != m_cacheGeneration) return;
    if (kind == "work" && listContext.contains("workRevision") && listContext.value("workRevision").toULongLong() != m_workRevision) return;
    if (kind == "comments" && listContext.contains("commentsRevision") && listContext.value("commentsRevision").toULongLong() != m_commentsRevision) return;
    if (listContext.contains("revision") && listContext.value("revision").toULongLong() != m_cardsRevision)
        return; // 已切换模式、榜单或排序，丢弃旧列表响应。
    const QString html = QString::fromUtf8(data);
    bool responseCached = listContext.value("cacheHit").toBool();
    auto saveResponse = [&] {
        if (!responseCached) { cachePage(url, data); responseCached = true; }
    };

    if (kind == QStringLiteral("rankcards") || kind == QStringLiteral("cards")) {
        if (!ok) {
            const QString message = kind == QStringLiteral("rankcards")
                ? QStringLiteral("获取排行榜失败: %1") : QStringLiteral("获取列表失败: %1");
            emit cardsFailed(message.arg(error));
            return;
        }
        emit searchPageReady(NovelParser::parseKakuyomuPageCount(html));
        const QVariantList cards = NovelParser::parseKakuyomuCards(html);
        if (html.contains(QStringLiteral("__NEXT_DATA__")))
            saveResponse();
        if (cards.isEmpty()) {
            emit cardsFailed(QStringLiteral("没有找到结果"));
        } else {
            saveResponse();
            emit cardsReady(cards);
        }
        return;
    }

    if (kind == QStringLiteral("authorusers")) {
        if (!ok) {
            emit cardsFailed(QStringLiteral("作者检索失败: %1").arg(error));
            return;
        }
        const QVariantList users = NovelParser::parseKakuyomuUsers(html);
        if (html.contains(QStringLiteral("__NEXT_DATA__"))) saveResponse();
        if (users.isEmpty()) {
            emit cardsFailed(QStringLiteral("没有找到该作者"));
            return;
        }
        saveResponse();
        const QString query = listContext.value(QStringLiteral("query")).toString().trimmed();
        QString normalized = query;
        if (normalized.startsWith(QLatin1Char('@')))
            normalized.remove(0, 1);
        QVariantMap selected = users.first().toMap();
        for (const QVariant &value : users) {
            const QVariantMap user = value.toMap();
            if (user.value(QStringLiteral("screenName")).toString().compare(normalized, Qt::CaseInsensitive) == 0
                || user.value(QStringLiteral("name")).toString().compare(normalized, Qt::CaseInsensitive) == 0
                || user.value(QStringLiteral("activityName")).toString().compare(normalized, Qt::CaseInsensitive) == 0) {
                selected = user;
                break;
            }
        }
        const QString screenName = selected.value(QStringLiteral("screenName")).toString();
        QString authorName = selected.value(QStringLiteral("activityName")).toString();
        if (authorName.isEmpty())
            authorName = selected.value(QStringLiteral("name")).toString();
        if (screenName.isEmpty()) {
            emit cardsFailed(QStringLiteral("该作者没有可用的主页"));
            return;
        }
        emit authorResolved(authorName);
        const QUrl worksUrl(QStringLiteral("https://kakuyomu.jp/users/%1/works")
                                .arg(QString::fromLatin1(QUrl::toPercentEncoding(screenName))));
        requestPage(worksUrl.toString(), QVariantMap{
            {"kind", "authorworks"}, {"revision", m_cardsRevision}
        });
        return;
    }

    if (kind == QStringLiteral("authorworks")) {
        if (!ok) {
            emit cardsFailed(QStringLiteral("获取作者作品失败: %1").arg(error));
            return;
        }
        emit searchPageReady(1);
        const QVariantList cards = NovelParser::parseKakuyomuAuthorWorks(html);
        if (html.contains(QStringLiteral("__NEXT_DATA__"))) saveResponse();
        if (cards.isEmpty())
            emit cardsFailed(QStringLiteral("该作者没有公开作品"));
        else
            { saveResponse(); emit cardsReady(cards); }
        return;
    }

    if (kind == QStringLiteral("work")) {
        if (!ok) {
            emit workOpenFailed(QStringLiteral("获取作品页失败: %1").arg(error));
            return;
        }
        const NovelWork work = NovelParser::parseWorkPage(
            NovelPaths::siteKeyFromUrl(url), html, url);
        if (!work.isValid() || work.chapters.isEmpty()) {
            emit workOpenFailed(QStringLiteral("未能解析出章节目录（页面结构可能已变化）"));
            return;
        }
        saveResponse();
        emit workOpened(work);
        const QVariantMap stats =
            NovelParser::parseKakuyomuWorkStats(html, work.workId);
        if (!stats.isEmpty())
            emit workStatsReady(stats);
        return;
    }

    if (kind == QStringLiteral("comments")) {
        if (!ok) {
            emit commentsFailed(QStringLiteral("获取评论失败: %1").arg(error));
            return;
        }
        const QVariantList comments = NovelParser::parseKakuyomuComments(html);
        if (html.contains(QStringLiteral("__NEXT_DATA__"))) saveResponse();
        if (comments.isEmpty())
            emit commentsFailed(QStringLiteral("还没有正文评价"));
        else
            { saveResponse(); emit commentsReady(comments); }
        return;
    }
}
