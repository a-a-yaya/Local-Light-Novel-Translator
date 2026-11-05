#include "novelpaths.h"

#include <QDir>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
// 支持的站点表：host（去掉 www.）/ 目录用的 siteKey / 界面显示名。
// siteKey 只保留字母数字，避免出现需要转义的目录名。
struct SiteDef
{
    const char *host;
    const char *key;
    const char *name;
};

const SiteDef kSites[] = {
    { "kakuyomu.jp",     "kakuyomujp",       "カクヨム" },
    { "novelup.plus",    "novelupplus",      "ノベルアップ＋" },
    { "alphapolis.co.jp","alphapoliscopjp",  "アルファポリス" },
    { "estar.jp",        "estarjp",          "エブリスタ" },
    { "syosetu.com",     "syosetucom",       "小説家になろう" },
    { "syosetu.org",     "syosetuorg",       "ハーメルン" },
    { "pixiv.net",       "pixivnet",         "pixiv 小说" },
    { "novel.daysneo.com", "noveldays",      "NOVEL DAYS" },
    { "novelba.com",     "novelba",          "ノベルバ" },
    { "solispia.com",    "solispia",         "ソリスピア" },
    { "pink.solispia.com", "solispiapink",    "ソリスピア Pink" },
};

const SiteDef *lookupSite(const QString &host)
{
    for (const SiteDef &s : kSites) {
        if (host == QLatin1String(s.host))
            return &s;
    }
    // 子域名，例如 ncode.syosetu.com / novel18.syosetu.com
    for (const SiteDef &s : kSites) {
        const QString suffix = QLatin1Char('.') + QLatin1String(s.host);
        if (host.endsWith(suffix))
            return &s;
    }
    return nullptr;
}
} // namespace

QString NovelPaths::hostOf(const QString &url)
{
    QString s = url.trimmed();
    if (s.isEmpty())
        return QString();
    if (!s.contains(QStringLiteral("://")))
        s.prepend(QStringLiteral("https://"));
    const QString host = QUrl(s).host().toLower();
    QString h = host;
    if (h.startsWith(QStringLiteral("www.")))
        h.remove(0, 4);
    return h;
}

QString NovelPaths::siteKeyFromHost(const QString &host)
{
    const QString h = hostOf(host);
    if (const SiteDef *site = lookupSite(h))
        return QString::fromUtf8(site->key);
    // 未登记的域名：退化成「域名去掉非字母数字」
    QString key;
    key.reserve(h.size());
    for (const QChar ch : h) {
        if (ch.isLetterOrNumber())
            key.append(ch.toLower());
    }
    return key;
}

QString NovelPaths::siteKeyFromUrl(const QString &url)
{
    return siteKeyFromHost(hostOf(url));
}

bool NovelPaths::isSupportedSite(const QString &host)
{
    return lookupSite(hostOf(host)) != nullptr;
}

QString NovelPaths::siteDisplayName(const QString &siteKey)
{
    for (const SiteDef &s : kSites) {
        if (siteKey == QLatin1String(s.key))
            return QString::fromUtf8(s.name);
    }
    return siteKey;
}

QString NovelPaths::workIdFromUrl(const QString &url, QString *error)
{
    if (error)
        error->clear();

    const QString host = hostOf(url);
    if (host.isEmpty()) {
        if (error)
            *error = QStringLiteral("无法识别网址，请填写完整的作品页地址");
        return QString();
    }
    if (!isSupportedSite(host)) {
        if (error)
            *error = QStringLiteral("未登记该站点：%1，请使用下载页列出的站点作品网址").arg(host);
        return QString();
    }

    const QString path = QUrl(url.contains(QStringLiteral("://"))
                                  ? url
                                  : QStringLiteral("https://") + url).path();

    QString id;
    if (host == QStringLiteral("novel.daysneo.com")) {
        id = QRegularExpression(QStringLiteral("^/works/([0-9a-f]{24,40})\\.html$"))
                 .match(path).captured(1);
    } else if (host == QStringLiteral("novelba.com")) {
        id = QRegularExpression(QStringLiteral("^/indies/works/(\\d+)(?:/episodes/\\d+)?/?$"))
                 .match(path).captured(1);
    } else if (host == QStringLiteral("solispia.com") || host == QStringLiteral("pink.solispia.com")) {
        id = QRegularExpression(QStringLiteral("^/title/(\\d+)/?$"))
                 .match(path).captured(1);
    } else if (host.endsWith(QStringLiteral("kakuyomu.jp"))) {
        id = QRegularExpression(QStringLiteral("works/(\\d+)")).match(path).captured(1);
    } else if (host == QStringLiteral("novelup.plus")) {
        id = QRegularExpression(QStringLiteral("story/(\\d+)")).match(path).captured(1);
    } else if (host == QStringLiteral("alphapolis.co.jp")) {
        // /novel/<作者ID>/<作品ID>：作品 ID 全局唯一
        id = QRegularExpression(QStringLiteral("novel/(\\d+)/(\\d+)"))
                 .match(path)
                 .captured(2);
    } else if (host == QStringLiteral("estar.jp")) {
        id = QRegularExpression(QStringLiteral("novels?/(\\d+)"))
                 .match(path)
                 .captured(1);
    } else if (host.endsWith(QStringLiteral("syosetu.com"))) {
        // なろう的作品编号是 ncode（n + 数字 + 字母），例如 n2267be
        static const QRegularExpression ncodeRe(QStringLiteral("^n[0-9a-z]+$"));
        const QStringList segments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString &seg : segments) {
            if (ncodeRe.match(seg).hasMatch()) {
                id = seg;
                break;
            }
        }
    } else if (host == QStringLiteral("syosetu.org")) {
        id = QRegularExpression(QStringLiteral("novel/(\\d+)")).match(path).captured(1);
    }

    if (host == QStringLiteral("pixiv.net")) {
        const QString series = QRegularExpression(QStringLiteral("^/(?:[a-z]{2}/)?novel/series/(\\d+)/?$"))
                                   .match(path).captured(1);
        if (!series.isEmpty())
            id = QStringLiteral("series_") + series;
        else if (QRegularExpression(QStringLiteral("^/(?:[a-z]{2}/)?novel/show\\.php$" )).match(path).hasMatch()) {
            const QString candidate = QUrlQuery(QUrl(url.contains("://") ? url : "https://" + url)).queryItemValue("id");
            if (QRegularExpression(QStringLiteral("^\\d+$")).match(candidate).hasMatch()) id = candidate;
        }
    }

    if (id.isEmpty() && error)
        *error = QStringLiteral("无法从该网址解析出作品编号：%1").arg(url);
    return id;
}

QString NovelPaths::workUrlFor(const QString &siteKey, const QString &workId)
{
    const QString id = workId.trimmed();
    if (id.isEmpty() || siteKey.isEmpty())
        return QString();
    for (const SiteDef &s : kSites) {
        if (siteKey != QLatin1String(s.key))
            continue;
        const QString host = QLatin1String(s.host);
        if (siteKey == QStringLiteral("noveldays"))
            return QStringLiteral("https://novel.daysneo.com/works/%1.html").arg(id);
        if (siteKey == QStringLiteral("novelba"))
            return QStringLiteral("https://novelba.com/indies/works/%1").arg(id);
        if (siteKey == QStringLiteral("solispia"))
            return QStringLiteral("https://solispia.com/title/%1").arg(id);
        if (siteKey == QStringLiteral("solispiapink"))
            return QStringLiteral("https://pink.solispia.com/title/%1").arg(id);
        if (host.endsWith(QStringLiteral("kakuyomu.jp")))
            return QStringLiteral("https://kakuyomu.jp/works/%1").arg(id);
        if (host == QStringLiteral("novelup.plus"))
            return QStringLiteral("https://novelup.plus/story/%1").arg(id);
        if (host == QStringLiteral("estar.jp"))
            return QStringLiteral("https://estar.jp/novels/%1").arg(id);
        if (host.endsWith(QStringLiteral("syosetu.com")))
            return QStringLiteral("https://ncode.syosetu.com/%1/").arg(id);
        if (host == QStringLiteral("syosetu.org"))
            return QStringLiteral("https://syosetu.org/novel/%1/").arg(id);
        if (host == QStringLiteral("pixiv.net"))
            return id.startsWith("series_")
                ? QStringLiteral("https://www.pixiv.net/novel/series/%1").arg(id.mid(7))
                : QStringLiteral("https://www.pixiv.net/novel/show.php?id=%1").arg(id);
        // alphapolis 的作品地址需要作者 ID，无法只凭 workId 复原
        return QString();
    }
    return QString();
}

QString NovelPaths::canonicalWorkUrl(const QString &url)
{
    const QString host = hostOf(url);
    QString error;
    const QString id = workIdFromUrl(url, &error);
    if (id.isEmpty())
        return url.trimmed();

    if (host == QStringLiteral("alphapolis.co.jp")) {
        const QString author =
            QRegularExpression(QStringLiteral("novel/(\\d+)/"))
                .match(QUrl(url).path())
                .captured(1);
        if (!author.isEmpty())
            return QStringLiteral("https://www.alphapolis.co.jp/novel/%1/%2").arg(author, id);
        return url.trimmed();
    }
    // 其它站点的规范作品地址与书库还原网址使用同一套规则。
    const QString canonical = workUrlFor(siteKeyFromHost(host), id);
    return canonical.isEmpty() ? url.trimmed() : canonical;
}

QString NovelPaths::novelRoot(const QString &downloadRoot, const QString &siteKey,
                              const QString &workId)
{
    if (downloadRoot.isEmpty() || workId.isEmpty())
        return QString();
    QDir dir(downloadRoot);
    if (siteKey.isEmpty())
        return dir.filePath(workId);
    return QDir(dir.filePath(siteKey)).filePath(workId);
}

QString NovelPaths::legacyNovelRoot(const QString &downloadRoot, const QString &workId)
{
    if (downloadRoot.isEmpty() || workId.isEmpty())
        return QString();
    return QDir(downloadRoot).filePath(workId);
}

QString NovelPaths::relativeKey(const QString &downloadRoot, const QString &novelRoot)
{
    if (novelRoot.isEmpty())
        return QString();
    if (downloadRoot.isEmpty())
        return QDir(novelRoot).dirName();
    QString rel = QDir(downloadRoot).relativeFilePath(novelRoot);
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.startsWith(QStringLiteral("../")))
        return QDir(novelRoot).dirName(); // 不在 download 根下：退化成单层目录名
    while (rel.endsWith(QLatin1Char('/')))
        rel.chop(1);
    return rel;
}

QString NovelPaths::translationChaptersDir(const QString &chdlRoot, const QString &relativeKey)
{
    if (chdlRoot.isEmpty() || relativeKey.isEmpty())
        return QString();
    return QDir(QDir(chdlRoot).filePath(relativeKey)).filePath(QStringLiteral("chapters"));
}

QString NovelPaths::bilingualChaptersDir(const QString &cjdlRoot, const QString &relativeKey)
{
    if (cjdlRoot.isEmpty() || relativeKey.isEmpty())
        return QString();
    return QDir(QDir(cjdlRoot).filePath(relativeKey)).filePath(QStringLiteral("chapters"));
}

QString NovelPaths::bilingualChapterFile(const QString &cjdlRoot, const QString &relativeKey,
                                         const QString &chapterBaseName)
{
    return QDir(bilingualChaptersDir(cjdlRoot, relativeKey))
        .filePath(chapterBaseName + QStringLiteral(".json"));
}
