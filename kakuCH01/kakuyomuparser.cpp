#include "kakuyomuparser.h"
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>

namespace {

// 递归收集页面内嵌 Apollo 缓存里的 "类型:id" 条目（Episode:... / TableOfContentsChapter:...
// / Chapter:... / Work:...），顺带记下第一个 tableOfContentsV2 作为兜底。
void collectApolloState(const QJsonValue &value, QHash<QString, QJsonObject> &refs,
                        QJsonArray *fallbackToc)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString key = it.key();
            const QJsonValue child = it.value();
            if (child.isObject() && key.contains(QLatin1Char(':')))
                refs.insert(key, child.toObject());
            if (fallbackToc->isEmpty() && child.isArray()
                && (key == QLatin1String("tableOfContentsV2")
                    || key == QLatin1String("tableOfContents")))
                *fallbackToc = child.toArray();
            collectApolloState(child, refs, fallbackToc);
        }
    } else if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        for (const QJsonValue &v : arr)
            collectApolloState(v, refs, fallbackToc);
    }
}

// kakuyomu 现在把目录按「章」折叠展示，初始 HTML 里只渲染了第一章的话，
// 完整目录其实在页面内嵌的 __NEXT_DATA__（Apollo 缓存）里：
//   Work:<id>.tableOfContentsV2            -> [TableOfContentsChapter:<id> ...]（按官方顺序）
//   TableOfContentsChapter:<id>.episodeUnions -> [Episode:<id> ...]
//   Episode:<id>                           -> { id, title, publishedAt }
// 只解析 HTML 可见目录会导致「只能下载前几章」，所以优先走这里。
bool parseTocFromNextData(const QString &html, NovelWork &work)
{
    static const QRegularExpression nextDataRe(
        QStringLiteral("<script[^>]*id=\"__NEXT_DATA__\"[^>]*>(.*?)</script>"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch match = nextDataRe.match(html);
    if (!match.hasMatch())
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(match.captured(1).toUtf8());
    if (!doc.isObject())
        return false;

    QHash<QString, QJsonObject> refs;
    QJsonArray fallbackToc;
    collectApolloState(doc.object(), refs, &fallbackToc);

    QJsonArray toc;
    if (!work.workId.isEmpty()) {
        const QJsonObject workObj = refs.value(QStringLiteral("Work:") + work.workId);
        toc = workObj.value(QStringLiteral("tableOfContentsV2")).toArray();
        if (toc.isEmpty())
            toc = workObj.value(QStringLiteral("tableOfContents")).toArray();
    }
    if (toc.isEmpty())
        toc = fallbackToc;
    if (toc.isEmpty())
        return false;

    QVector<Chapter> chapters;
    for (const QJsonValue &chapterRef : toc) {
        const QJsonObject chapter =
            refs.value(chapterRef.toObject().value(QStringLiteral("__ref")).toString());
        const QJsonArray unions = chapter.value(QStringLiteral("episodeUnions")).toArray();
        for (const QJsonValue &episodeRef : unions) {
            const QString ref =
                episodeRef.toObject().value(QStringLiteral("__ref")).toString();
            const QJsonObject episode = refs.value(ref);

            Chapter ch;
            ch.episodeId = episode.value(QStringLiteral("id")).toString();
            if (ch.episodeId.isEmpty() && ref.startsWith(QStringLiteral("Episode:")))
                ch.episodeId = ref.mid(QStringLiteral("Episode:").size());
            if (ch.episodeId.isEmpty())
                continue;
            ch.title = episode.value(QStringLiteral("title")).toString().trimmed();
            ch.episodeUrl = QStringLiteral("https://kakuyomu.jp/works/%1/episodes/%2")
                                .arg(work.workId, ch.episodeId);
            if (!ch.isValid())
                continue;
            ch.number = chapters.size() + 1; // 与官方目录一致的稳定序号
            chapters.append(ch);
        }
    }
    if (chapters.isEmpty())
        return false;
    work.chapters = chapters;
    return true;
}

} // namespace

NovelWork KakuyomuParser::parseWorkPage(const QString &html, const QString &workUrl)
{
    NovelWork work;
    work.workUrl = workUrl;

    // 从网址中提取书籍编号，例如 works/16817330669718495903
    QRegularExpression idRe("works/(\\d+)");
    QRegularExpressionMatch idMatch = idRe.match(workUrl);
    if (idMatch.hasMatch())
        work.workId = idMatch.captured(1);

    // 标题优先取 og:title，缺失时回退到 <title> 标签
    QRegularExpression ogRe("<meta[^>]*property=[\"']og:title[\"'][^>]*content=[\"']([^\"']+)[\"']");
    QRegularExpressionMatch ogMatch = ogRe.match(html);
    if (ogMatch.hasMatch()) {
        work.title = decodeEntities(ogMatch.captured(1));
    } else {
        QRegularExpression titleRe("<title[^>]*>(.*?)</title>",
                                   QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch tMatch = titleRe.match(html);
        if (tMatch.hasMatch())
            work.title = decodeEntities(tMatch.captured(1));
    }
    // 去掉标题末尾的站点后缀
    const QString suffix = QStringLiteral(" - カクヨム");
    if (work.title.endsWith(suffix))
        work.title.chop(suffix.size());

    // 作者名：从页面内嵌的 dataLayer 中提取 workAuthorName（无法解析时留空）
    QRegularExpression authorRe(QStringLiteral("\"workAuthorName\"\\s*:\\s*\"([^\"]+)\""));
    QRegularExpressionMatch authorMatch = authorRe.match(html);
    if (authorMatch.hasMatch())
        work.author = authorMatch.captured(1);

    // 章节列表：优先读内嵌 JSON 的完整目录（HTML 里只渲染了第一章的话）；
    // 取不到时回退到 HTML 可见目录，保证站点改版时不至于完全失效。
    if (!parseTocFromNextData(html, work)) {
        // 一条正则同时捕获“链接”和“标题”
        QRegularExpression chapterRe(
            QStringLiteral("WorkTocSection_link__ocg9K\\s*\"\\s*href=\"([^\"]+)\".*?"
                           "WorkTocSection_title__H2007.*?<div>([^<]+)</div>"),
            QRegularExpression::DotMatchesEverythingOption);

        QRegularExpressionMatchIterator it = chapterRe.globalMatch(html);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            Chapter ch;
            ch.title = decodeEntities(m.captured(2)).trimmed();

            // 相对路径补全为绝对地址
            QString path = m.captured(1);
            if (!path.startsWith(QStringLiteral("http")))
                path.prepend(QStringLiteral("https://kakuyomu.jp"));
            ch.episodeUrl = path;

            // 提取章节编号
            QRegularExpression epIdRe("episodes/(\\d+)");
            QRegularExpressionMatch epMatch = epIdRe.match(path);
            if (epMatch.hasMatch())
                ch.episodeId = epMatch.captured(1);

            if (ch.isValid()) {
                ch.number = work.chapters.size() + 1; // 稳定序号，从 1 开始
                work.chapters.append(ch);
            }
        }
    }

    return work;
}

QString KakuyomuParser::parseEpisodeBody(const QString &html)
{
    // 正文位于 widget-episodeBody 容器内，截取到其结束标签
    QRegularExpression bodyRe(QStringLiteral("widget-episodeBody[^>]*>(.*?)</div>"),
                              QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch bodyMatch = bodyRe.match(html);
    if (!bodyMatch.hasMatch())
        return QString();
    QString body = bodyMatch.captured(1);

    // 逐段提取 <p>，空段落（class="blank"）作为换行分隔
    QStringList paragraphs;
    QRegularExpression pRe(QStringLiteral("<p[^>]*>(.*?)</p>"),
                           QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = pRe.globalMatch(body);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QString full = m.captured(0);
        QString inner = m.captured(1);
        if (full.contains(QStringLiteral("class=\"blank\"")))
            paragraphs << QString();
        else
            paragraphs << stripHtml(inner);
    }
    return paragraphs.join(QLatin1Char('\n'));
}

QString KakuyomuParser::stripHtml(const QString &fragment)
{
    QString s = fragment;
    // 去掉 ruby 注音（只保留汉字本身）
    s.remove(QRegularExpression(QStringLiteral("<rp>.*?</rp>"),
                                QRegularExpression::DotMatchesEverythingOption));
    s.remove(QRegularExpression(QStringLiteral("<rt>.*?</rt>"),
                                QRegularExpression::DotMatchesEverythingOption));
    s.remove(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                QRegularExpression::CaseInsensitiveOption));
    // 去掉其余所有 HTML 标签
    s.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    return decodeEntities(s).trimmed();
}

QString KakuyomuParser::decodeEntities(const QString &text)
{
    QString s = text;
    // 数字实体（十进制），如 &#12354;
    QRegularExpression decRe(QStringLiteral("&#(\\d+);"));
    int guard = 0;
    while (guard++ < 100) {
        QRegularExpressionMatch m = decRe.match(s);
        if (!m.hasMatch())
            break;
        s.replace(m.capturedStart(), m.capturedLength(), QChar(m.captured(1).toInt()));
    }
    // 数字实体（十六进制），如 &#x3042;
    QRegularExpression hexRe(QStringLiteral("&#x([0-9a-fA-F]+);"));
    guard = 0;
    while (guard++ < 100) {
        QRegularExpressionMatch m = hexRe.match(s);
        if (!m.hasMatch())
            break;
        bool ok = false;
        int cp = m.captured(1).toInt(&ok, 16);
        s.replace(m.capturedStart(), m.capturedLength(), QChar(cp));
    }
    // 常见命名实体
    s.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    s.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    s.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    return s;
}

QString KakuyomuParser::sanitizeFileName(const QString &name)
{
    // 去除文件名中不允许的字符
    QString s = name;
    s.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    s = s.trimmed();
    if (s.isEmpty())
        s = QStringLiteral("untitled");
    return s;
}
