#include "novelparser.h"
#include "kakuyomuparser.h"
#include "novelpaths.h"

#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>
#include <QSet>
#include <QHash>
#include <QDateTime>
#include <QTimeZone>

namespace {
const QRegularExpression::PatternOptions kDotAll =
    QRegularExpression::DotMatchesEverythingOption;

QJsonObject kakuyomuPageData(const QString &html)
{
    static const QRegularExpression re(
        QStringLiteral("<script[^>]*id=\"__NEXT_DATA__\"[^>]*>(.*?)</script>"), kDotAll);
    return QJsonDocument::fromJson(re.match(html).captured(1).toUtf8()).object();
}

QJsonObject kakuyomuListConnection(const QJsonObject &data, bool *ranking = nullptr)
{
    const auto state = data.value("props").toObject().value("pageProps").toObject()
                           .value("__APOLLO_STATE__").toObject();
    const auto root = state.value("ROOT_QUERY").toObject();
    const bool isRanking = data.value("page").toString().startsWith("/rankings/");
    if (ranking) *ranking = isRanking;
    const QString prefix = isRanking ? QStringLiteral("rankedWorks(")
                                     : QStringLiteral("searchWorks(");
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key().startsWith(prefix))
            return it.value().toObject();
    }
    return {};
}

// 取 <meta property="og:title" content="...">
QString ogTitle(const QString &html)
{
    static const QRegularExpression re(
        QStringLiteral("<meta[^>]*property=[\"']og:title[\"'][^>]*content=[\"']([^\"']*)[\"']"));
    return NovelParser::decodeEntities(re.match(html).captured(1)).trimmed();
}

// 取 <title> 作为兜底
QString tagTitle(const QString &html)
{
    static const QRegularExpression re(QStringLiteral("<title[^>]*>(.*?)</title>"), kDotAll);
    return NovelParser::decodeEntities(re.match(html).captured(1)).trimmed();
}

// 去掉标题末尾的「 | 站点名」后缀
QString stripSiteSuffix(const QString &title)
{
    QString t = title;
    const int sep = t.lastIndexOf(QStringLiteral(" | "));
    if (sep > 0)
        t = t.left(sep);
    return t.trimmed();
}

// 从内嵌 JSON 里取 "author":{... "name":"..."}，注意 \uXXXX 转义
QString authorFromJson(const QString &html)
{
    static const QRegularExpression re(
        QStringLiteral("\"author\"\\s*:\\s*\\{.*?\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""),
        kDotAll);
    const QString raw = re.match(html).captured(1);
    return NovelParser::decodeJsonEscapes(raw).trimmed();
}

// ---------------- novelup.plus ----------------

NovelWork parseNovelupWorkPage(const QString &html, const QString &workUrl)
{
    NovelWork work;
    work.workUrl = workUrl;
    work.siteKey = QStringLiteral("novelupplus");

    static const QRegularExpression idRe(QStringLiteral("story/(\\d+)"));
    work.workId = idRe.match(workUrl).captured(1);

    work.title = stripSiteSuffix(ogTitle(html).isEmpty() ? tagTitle(html) : ogTitle(html));
    work.author = authorFromJson(html);

    // 章节列表：<a class="episodeTitle" href="…/story/<id>/<epId>" data-number="005">标题</a>
    static const QRegularExpression epRe(
        QStringLiteral("<a\\b([^>]*\\bclass=\"[^\"]*\\bepisodeTitle\\b[^\"]*\"[^>]*)>(.*?)</a>"), kDotAll);
    static const QRegularExpression hrefRe(QStringLiteral("href=\"([^\"]+)\""));
    static const QRegularExpression epIdRe(QStringLiteral("story/\\d+/(\\d+)"));

    QRegularExpressionMatchIterator it = epRe.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString attrs = m.captured(1);
        QString url = hrefRe.match(attrs).captured(1);
        if (url.isEmpty())
            continue;
        if (!url.startsWith(QStringLiteral("http")))
            url.prepend(QStringLiteral("https://novelup.plus"));

        Chapter ch;
        ch.episodeUrl = url;
        ch.episodeId = epIdRe.match(url).captured(1);
        ch.title = NovelParser::stripHtml(m.captured(2));
        if (!ch.isValid())
            continue;
        ch.number = QRegularExpression(QStringLiteral("data-number=\"(\\d+)\"")).match(attrs).captured(1).toInt();
        if (ch.number < 1) ch.number = work.chapters.size() + 1;
        work.chapters.append(ch);
    }
    return work;
}

QString parseNovelupEpisodeBody(const QString &html)
{
    static const QRegularExpression re(
        QStringLiteral("<p[^>]*id=\"episode_content\"[^>]*>(.*?)</p>"), kDotAll);
    const QString body = re.match(html).captured(1);
    if (body.isEmpty())
        return QString();
    // 段落之间以换行分隔；去掉可能的标签与实体
    QString fragment = body;
    fragment.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption), "\n");
    QString text = NovelParser::stripHtml(fragment);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar(0x2028), QLatin1Char('\n'));
    return text.trimmed();
}

// ---------------- alphapolis.co.jp ----------------

// 单话正文：<div id="novelBody"> … <br /> … </div>（内部没有嵌套 div）
QString parseAlphapolisEpisodeBody(const QString &html)
{
    const int idPos = html.indexOf(QStringLiteral("id=\"novelBody\""));
    if (idPos < 0)
        return QString();
    const int start = html.indexOf(QLatin1Char('>'), idPos);
    if (start < 0)
        return QString();
    const int end = html.indexOf(QStringLiteral("</div>"), start);
    if (end < 0)
        return QString();

    QString inner = html.mid(start + 1, end - start - 1);
    inner.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                     QRegularExpression::CaseInsensitiveOption),
                  QStringLiteral("\n"));
    const QString text = NovelParser::stripHtml(inner);

    // 逐行清理：去首尾空白、丢弃空行，保持“一段一行”，与其它站点一致。
    QStringList lines;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (!line.isEmpty())
            lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

NovelWork parseAlphapolisWorkPage(const QString &html, const QString &workUrl)
{
    NovelWork work;
    work.workUrl = workUrl;
    work.siteKey = QStringLiteral("alphapoliscopjp");

    // /novel/<作者ID>/<作品ID>
    static const QRegularExpression idRe(QStringLiteral("/novel/(\\d+)/(\\d+)"));
    work.workId = idRe.match(QUrl(workUrl).path()).captured(2);

    QString title = ogTitle(html);
    if (title.isEmpty())
        title = tagTitle(html);
    title = title.section(QStringLiteral(" | "), 0, 0).trimmed();
    if (title.startsWith(QStringLiteral("『")) && title.endsWith(QStringLiteral("』")))
        title = title.mid(1, title.size() - 2);
    work.title = title;
    work.author = authorFromJson(html);

    // 章节目录内嵌在页面的 JSON 里：
    //   "episodes":[{"chapterId":…,"title":"第一章","episodes":[
    //        {"episodeNo":11903142,"url":"\/novel\/…\/episode\/11903142",
    //         "mainTitle":"第一話　…", …}]}]
    static const QRegularExpression epRe(QStringLiteral(
        "\"episodeNo\":(\\d+),\"url\":\"((?:[^\"\\\\]|\\\\.)*)\","
        "\"mainTitle\":\"((?:[^\"\\\\]|\\\\.)*)\""));
    QRegularExpressionMatchIterator it = epRe.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString url = NovelParser::decodeJsonEscapes(m.captured(2));
        if (!url.startsWith(QStringLiteral("http")))
            url.prepend(QStringLiteral("https://www.alphapolis.co.jp"));

        Chapter ch;
        ch.episodeId = m.captured(1);
        ch.episodeUrl = url;
        ch.title = NovelParser::decodeJsonEscapes(m.captured(3)).trimmed();
        if (!ch.isValid())
            continue;
        ch.number = work.chapters.size() + 1;
        work.chapters.append(ch);
    }
    return work;
}
// Extract a complete div, including nested markup; never spill into navigation.
QString divContents(const QString &html, int opening)
{
    const int start = html.indexOf('>', opening);
    if (opening < 0 || start < 0) return {};
    const QRegularExpression tag(QStringLiteral("</?div\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    auto it = tag.globalMatch(html, start + 1);
    int depth = 1;
    while (it.hasNext()) {
        const auto m = it.next();
        depth += m.captured().startsWith("</") ? -1 : 1;
        if (depth == 0) return html.mid(start + 1, m.capturedStart() - start - 1);
    }
    return {};
}

QString proseFromHtml(QString fragment)
{
    fragment.replace(QRegularExpression(QStringLiteral("<br\\s*/?>|</p\\s*>"),
                                       QRegularExpression::CaseInsensitiveOption), "\n");
    return NovelParser::stripHtml(fragment).trimmed();
}

QString htmlAttribute(const QString &attributes, const QString &name)
{
    const QRegularExpression re(QStringLiteral("(?:^|\\s)%1\\s*=\\s*([\"'])(.*?)\\1")
        .arg(QRegularExpression::escape(name)), kDotAll);
    return NovelParser::decodeEntities(re.match(attributes).captured(2));
}

// Match a class token, then keep extraction inside that element.
QString classContents(const QString &html, const QString &tag, const QString &token)
{
    const QRegularExpression re(QStringLiteral("<%1\\b([^>]*)>").arg(tag));
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        if (!htmlAttribute(m.captured(1), "class").split(QRegularExpression("\\s+")).contains(token))
            continue;
        if (tag == "div") return divContents(html, m.capturedStart());
        const int end = html.indexOf("</" + tag + ">", m.capturedEnd());
        if (end >= 0) return html.mid(m.capturedEnd(), end - m.capturedEnd());
    }
    return {};
}

NovelWork parsePublicNovelWork(const QString &site, const QString &html, const QString &url)
{
    if (site == "solispiapink" && html.contains("/r18/proceed")) return {};
    NovelWork work;
    work.siteKey = site;
    work.workId = NovelPaths::workIdFromUrl(url);
    work.workUrl = NovelPaths::canonicalWorkUrl(url);
    QString catalogue;
    QString episodePattern;
    if (site == "noveldays") {
        // Chat novels use a different format; only ordinary prose is supported.
        if (tagTitle(html).contains(QStringLiteral("チャットノベル作品詳細"))) return {};
        const QRegularExpression titleRe(QStringLiteral("<h2\\b[^>]*>(.*?)</h2>"), kDotAll);
        work.title = NovelParser::stripHtml(titleRe.match(html).captured(1));
        const QRegularExpression authorRe(QStringLiteral("<a\\b[^>]*href=[\"']/author/[^\"']+[\"'][^>]*>(.*?)</a>"), kDotAll);
        work.author = NovelParser::stripHtml(authorRe.match(html).captured(1));
        catalogue = classContents(html, "div", "contents");
        episodePattern = "^/works/episode/([0-9a-f]{24,40})\\.html$";
    } else if (site == "novelba") {
        work.title = NovelParser::stripHtml(classContents(html, "h1", "title"));
        work.author = NovelParser::stripHtml(classContents(html, "p", "author"));
        catalogue = classContents(html, "ul", "episode_list");
        episodePattern = QStringLiteral("^/indies/works/%1/episodes/(\\d+)/?$").arg(work.workId);
    } else {
        work.title = NovelParser::stripHtml(classContents(html, "h1", "text-title"));
        work.author = NovelParser::stripHtml(classContents(html, "div", "author"));
        catalogue = classContents(html, "div", "episode");
        episodePattern = "^/novel/(\\d+)/?$";
    }
    if (work.title.isEmpty()) return {};
    const QRegularExpression anchor(QStringLiteral("<a\\b([^>]*)>(.*?)</a>"), kDotAll);
    const QRegularExpression episode(episodePattern);
    const QRegularExpression span(QStringLiteral("<span\\b[^>]*>(.*?)</span>"), kDotAll);
    QSet<QString> seen;
    int ordinal = 0;
    auto it = anchor.globalMatch(catalogue);
    while (it.hasNext()) {
        const auto m = it.next();
        const QUrl target = QUrl(work.workUrl).resolved(QUrl(htmlAttribute(m.captured(1), "href")));
        if (target.host() != QUrl(work.workUrl).host()) continue;
        const QString id = episode.match(target.path()).captured(1);
        if (id.isEmpty() || seen.contains(id)) continue;
        seen.insert(id);
        ++ordinal;
        // Preserve the original ordinal when paid episodes are omitted.
        if ((site == "solispia" || site == "solispiapink") && htmlAttribute(m.captured(1), "data-is-paid") != "0") continue;
        Chapter ch;
        ch.number = ordinal;
        ch.episodeId = id;
        ch.episodeUrl = target.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
        if (site == "novelba") ch.title = NovelParser::stripHtml(classContents(m.captured(2), "div", "episode_title"));
        else if (site == "solispia" || site == "solispiapink") ch.title = htmlAttribute(m.captured(1), "data-subtitle");
        else ch.title = NovelParser::stripHtml(span.match(m.captured(2)).captured(1));
        if (ch.title.isEmpty()) ch.title = QStringLiteral("第 %1 章").arg(ordinal);
        work.chapters.append(ch);
    }
    return work;
}

QString parsePublicNovelBody(const QString &site, const QString &html)
{
    if (site == "solispiapink" && html.contains("/r18/proceed")) return {};
    QString fragment;
    if (site == "noveldays") fragment = classContents(classContents(html, "div", "episode"), "div", "inner");
    else if (site == "novelba") fragment = classContents(classContents(html, "div", "episode_box"), "div", "detail");
    else {
        const QRegularExpression re(QStringLiteral("<div\\b[^>]*\\bid=[\"']novelContent[\"'][^>]*>"));
        fragment = divContents(html, re.match(html).capturedStart());
        // Solispia stores paragraphs as nested divs, plus a hidden vertical copy.
        fragment.replace(QRegularExpression(QStringLiteral("</div\\s*>")), "\n");
    }
    fragment.remove(QRegularExpression(QStringLiteral("<(?:script|style)\\b[^>]*>.*?</(?:script|style)>"), kDotAll));
    return proseFromHtml(fragment);
}

QJsonArray nuxtData(const QString &html)
{
    const QRegularExpression re(QStringLiteral("<script[^>]*id=[\"']__NUXT_DATA__[\"'][^>]*>(.*?)</script>"), kDotAll);
    return QJsonDocument::fromJson(re.match(html).captured(1).toUtf8()).array();
}

QJsonValue nuxtValue(const QJsonArray &data, const QJsonValue &reference)
{
    if (!reference.isDouble()) return {};
    const int index = reference.toInt(-1);
    return index >= 0 && index < data.size() ? data[index] : QJsonValue();
}

NovelWork parseEstarWorkPage(const QString &html, const QString &url)
{
    NovelWork work;
    const QString id = NovelPaths::workIdFromUrl(url);
    const auto data = nuxtData(html);
    for (const auto &entry : data) {
        const auto obj = entry.toObject();
        if (!obj.contains("publishedPageCount") || nuxtValue(data, obj.value("workId")).toString() != id) continue;
        work.workId = id;
        work.siteKey = "estarjp";
        work.workUrl = NovelPaths::canonicalWorkUrl(url);
        work.title = nuxtValue(data, obj.value("title")).toString();
        const auto user = nuxtValue(data, obj.value("user")).toObject();
        work.author = nuxtValue(data, user.value("nickname")).toString();
        const int count = nuxtValue(data, obj.value("publishedPageCount")).toInt();
        if (count < 1 || count > 100000 || nuxtValue(data, obj.value("isRestricted")).toBool()) return {};
        for (int page = 1; page <= count; ++page) {
            Chapter ch;
            ch.number = page;
            ch.episodeId = QString::number(page);
            ch.title = QStringLiteral("第 %1 页").arg(page);
            ch.episodeUrl = work.workUrl + QStringLiteral("/viewer?page=%1").arg(page);
            work.chapters.append(ch);
        }
        return work;
    }
    return {};
}

QString parseEstarEpisodeBody(const QString &html)
{
    const QRegularExpression re(QStringLiteral("<div\\b[^>]*class=[\"'][^\"']*-unselectable\\s+content\\b[^\"']*[\"'][^>]*>"));
    const auto match = re.match(html);
    return match.hasMatch() ? proseFromHtml(divContents(html, match.capturedStart())) : QString();
}

NovelWork parsePixivWorkPage(const QString &json, const QString &url)
{
    const auto response = QJsonDocument::fromJson(json.toUtf8()).object();
    if (response.isEmpty() || response.value("error").toBool()) return {};
    NovelWork work;
    work.workId = NovelPaths::workIdFromUrl(url);
    work.workUrl = NovelPaths::canonicalWorkUrl(url);
    work.siteKey = "pixivnet";
    const auto body = response.value("body");
    if (body.isArray() && work.workId.startsWith("series_")) {
        int ordinal = 0;
        for (const auto &entry : body.toArray()) {
            ++ordinal;
            const auto obj = entry.toObject();
            const QString id = obj.value("id").toString();
            if (!obj.value("available").toBool() || !QRegularExpression("^\\d+$").match(id).hasMatch()) continue;
            Chapter ch;
            ch.number = ordinal;
            ch.episodeId = id;
            ch.title = obj.value("title").toString();
            ch.episodeUrl = NovelPaths::workUrlFor(work.siteKey, id);
            work.chapters.append(ch);
        }
        return work;
    }
    const auto obj = body.toObject();
    if (obj.isEmpty() || obj.value("isLoginOnly").toBool()) return {};
    work.title = obj.value("title").toString();
    work.author = obj.value("userName").toString();
    if (!work.workId.startsWith("series_") && !obj.value("content").toString().trimmed().isEmpty()) {
        Chapter ch;
        ch.number = 1;
        ch.episodeId = work.workId;
        ch.title = work.title;
        ch.episodeUrl = work.workUrl;
        work.chapters.append(ch);
    }
    return work;
}

QString parsePixivEpisodeBody(const QString &json)
{
    const auto response = QJsonDocument::fromJson(json.toUtf8()).object();
    if (response.isEmpty() || response.value("error").toBool()) return {};
    const auto obj = response.value("body").toObject();
    if (obj.value("isLoginOnly").toBool()) return {};
    QString text = obj.value("content").toString();
    text.replace("[newpage]", "\n\n");
    text.replace(QRegularExpression("\\[chapter:([^\\]]+)\\]"), "\n\n\\1\n");
    text.replace(QRegularExpression("\\[\\[rb:([^>\\]]+)\\s*>[^\\]]*\\]\\]"), "\\1");
    text.replace(QRegularExpression("\\[\\[jumpuri:([^>\\]]+)>\\s*([^\\]]+)\\]\\]"), "\\1（\\2）");
    text.replace(QRegularExpression("\\[(?:pixivimage|uploadedimage):[^\\]]+\\]"), QStringLiteral("〔插图〕"));
    text.replace(QRegularExpression("\\[jump:(\\d+)\\]"), QStringLiteral("（跳转至第 \\1 页）"));
    return text.trimmed();
}

NovelWork parseNarouWorkPage(const QString &html, const QString &url)
{
    NovelWork work;
    work.siteKey = "syosetucom";
    work.workId = NovelPaths::workIdFromUrl(url);
    work.workUrl = NovelPaths::canonicalWorkUrl(url);
    work.title = stripSiteSuffix(ogTitle(html).isEmpty() ? tagTitle(html) : ogTitle(html));
    const QRegularExpression author(QStringLiteral("<(?:div|p)[^>]*class=[\"'][^\"']*(?:p-novel__author|novel_writername)[^\"']*[\"'][^>]*>(.*?)</(?:div|p)>"), kDotAll);
    work.author = NovelParser::stripHtml(author.match(html).captured(1));
    work.author.remove(QRegularExpression(QStringLiteral("^作者[：:]\\s*")));
    const QRegularExpression link(QStringLiteral("<a\\b[^>]*href=[\"']([^\"']+)[\"'][^>]*>(.*?)</a>"), kDotAll);
    auto it = link.globalMatch(html);
    QSet<QString> seen;
    while (it.hasNext()) {
        const auto m = it.next();
        const QUrl episode = QUrl(url).resolved(QUrl(NovelParser::decodeEntities(m.captured(1))));
        const auto ep = QRegularExpression("^/" + QRegularExpression::escape(work.workId) + "/(\\d+)/?$").match(episode.path());
        if (!ep.hasMatch() || episode.host() != QUrl(work.workUrl).host() || seen.contains(ep.captured(1))) continue;
        seen.insert(ep.captured(1));
        Chapter ch;
        ch.number = ep.captured(1).toInt();
        ch.episodeId = ep.captured(1);
        ch.episodeUrl = episode.toString();
        ch.title = NovelParser::stripHtml(m.captured(2));
        work.chapters.append(ch);
    }
    if (work.chapters.isEmpty() && (html.contains("id=\"novel_honbun\"") || html.contains("p-novel__text"))) {
        Chapter ch;
        ch.number = 1; ch.episodeId = work.workId; ch.title = work.title; ch.episodeUrl = work.workUrl;
        work.chapters.append(ch);
    }
    return work;
}

QString parseNarouEpisodeBody(const QString &html)
{
    const QRegularExpression re(QStringLiteral("<div\\b[^>]*(?:id=[\"']novel_honbun[\"']|class=[\"'][^\"']*\\bp-novel__text\\b[^\"']*[\"'])[^>]*>"));
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.captured().contains("--preface") || m.captured().contains("--afterword")) continue;
        return proseFromHtml(divContents(html, m.capturedStart()));
    }
    return {};
}
} // namespace

QByteArray NovelParser::solispiaAgeConfirmationForm(const QString &html, const QString &returnUrl)
{
    const QUrl target(returnUrl);
    if (target.scheme() != "https" || target.host() != "pink.solispia.com"
        || !QRegularExpression("^/(?:title|novel)/\\d+/?$").match(target.path()).hasMatch()) return {};
    const QRegularExpression form(QStringLiteral("<form\\b([^>]*)>(.*?)</form>"), kDotAll);
    const QRegularExpression input(QStringLiteral("<input\\b([^>]*)>"));
    auto forms = form.globalMatch(html);
    while (forms.hasNext()) {
        const auto m = forms.next();
        const QUrl action = target.resolved(QUrl(htmlAttribute(m.captured(1), "action")));
        if (action != QUrl("https://pink.solispia.com/r18/proceed")
            || htmlAttribute(m.captured(1), "method").compare("post", Qt::CaseInsensitive) != 0) continue;
        auto inputs = input.globalMatch(m.captured(2));
        while (inputs.hasNext()) {
            const QString attrs = inputs.next().captured(1);
            if (htmlAttribute(attrs, "name") != "_token") continue;
            const QString token = htmlAttribute(attrs, "value");
            if (token.isEmpty()) return {};
            // Use the requested HTTPS page, never an untrusted hidden redirect.
            return "_token=" + QUrl::toPercentEncoding(token) + "&intended="
                + QUrl::toPercentEncoding(target.toString(QUrl::RemoveQuery | QUrl::RemoveFragment));
        }
    }
    return {};
}

bool NovelParser::supportsParsing(const QString &siteKey)
{
    return siteKey == QStringLiteral("kakuyomujp")
           || siteKey == QStringLiteral("novelupplus")
           || siteKey == QStringLiteral("alphapoliscopjp")
           || siteKey == QStringLiteral("estarjp")
           || siteKey == QStringLiteral("syosetucom")
           || siteKey == QStringLiteral("noveldays")
           || siteKey == QStringLiteral("novelba")
           || siteKey == QStringLiteral("solispia")
           || siteKey == QStringLiteral("solispiapink")
           || siteKey == QStringLiteral("pixivnet");
}

NovelWork NovelParser::parseWorkPage(const QString &siteKey, const QString &html,
                                     const QString &workUrl)
{
    if (siteKey == QStringLiteral("kakuyomujp")) {
        NovelWork w = KakuyomuParser::parseWorkPage(html, workUrl);
        w.siteKey = siteKey;
        return w;
    }
    if (siteKey == QStringLiteral("novelupplus"))
        return parseNovelupWorkPage(html, workUrl);
    if (siteKey == QStringLiteral("alphapoliscopjp"))
        return parseAlphapolisWorkPage(html, workUrl);
    if (siteKey == "estarjp") return parseEstarWorkPage(html, workUrl);
    if (siteKey == "pixivnet") return parsePixivWorkPage(html, workUrl);
    if (siteKey == "syosetucom") return parseNarouWorkPage(html, workUrl);
    if (siteKey == "noveldays" || siteKey == "novelba" || siteKey == "solispia" || siteKey == "solispiapink")
        return parsePublicNovelWork(siteKey, html, workUrl);
    return NovelWork();
}

QString NovelParser::parseEpisodeBody(const QString &siteKey, const QString &html)
{
    if (siteKey == QStringLiteral("kakuyomujp"))
        return KakuyomuParser::parseEpisodeBody(html);
    if (siteKey == QStringLiteral("novelupplus"))
        return parseNovelupEpisodeBody(html);
    if (siteKey == QStringLiteral("alphapoliscopjp"))
        return parseAlphapolisEpisodeBody(html);
    if (siteKey == "estarjp") return parseEstarEpisodeBody(html);
    if (siteKey == "pixivnet") return parsePixivEpisodeBody(html);
    if (siteKey == "syosetucom") return parseNarouEpisodeBody(html);
    if (siteKey == "noveldays" || siteKey == "novelba" || siteKey == "solispia" || siteKey == "solispiapink")
        return parsePublicNovelBody(siteKey, html);
    return QString();
}

QString NovelParser::nextWorkPage(const QString &siteKey, const QString &html, const QString &pageUrl)
{
    if (siteKey != "novelupplus" && siteKey != "syosetucom") return {};
    const QUrl base(pageUrl);
    const int current = qMax(1, QUrlQuery(base).queryItemValue("p").toInt());
    const QRegularExpression re(QStringLiteral("<a\\b[^>]*href=[\"']([^\"']+)[\"'][^>]*>"));
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const QUrl next = base.resolved(QUrl(decodeEntities(it.next().captured(1))));
        if (next.host() == base.host() && next.path() == base.path()
            && QUrlQuery(next).queryItemValue("p").toInt() == current + 1) return next.toString();
    }
    return {};
}

QString NovelParser::decodeEntities(const QString &text)
{
    return KakuyomuParser::decodeEntities(text);
}

QString NovelParser::sanitizeFileName(const QString &name)
{
    return KakuyomuParser::sanitizeFileName(name);
}

QString NovelParser::stripHtml(const QString &fragment)
{
    QString s = fragment;
    s.remove(QRegularExpression(QStringLiteral("<rp>.*?</rp>"), kDotAll));
    s.remove(QRegularExpression(QStringLiteral("<rt>.*?</rt>"), kDotAll));
    s.remove(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression(QStringLiteral("<[^>]+>"), kDotAll));
    return decodeEntities(s).trimmed();
}

QString NovelParser::decodeJsonEscapes(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != QLatin1Char('\\') || i + 1 >= text.size()) {
            out.append(ch);
            continue;
        }
        const QChar next = text.at(i + 1);
        switch (next.unicode()) {
        case 'n': out.append(QLatin1Char('\n')); ++i; break;
        case 'r': out.append(QLatin1Char('\r')); ++i; break;
        case 't': out.append(QLatin1Char('\t')); ++i; break;
        case '"': out.append(QLatin1Char('"')); ++i; break;
        case '\\': out.append(QLatin1Char('\\')); ++i; break;
        case '/': out.append(QLatin1Char('/')); ++i; break;
        case 'u': {
            if (i + 5 < text.size()) {
                bool ok = false;
                const uint code = text.mid(i + 2, 4).toUInt(&ok, 16);
                if (ok) {
                    out.append(QChar(code));
                    i += 5;
                    break;
                }
            }
            out.append(ch);
            break;
        }
        default:
            out.append(ch);
            break;
        }
    }
    return out;
}

// ---------------- 「网站」页：カクヨム浏览解析 ----------------

QVariantList NovelParser::parseKakuyomuCards(const QString &html)
{
    QVariantList cards;
    QSet<QString> seen;

    // 只取当前列表 connection 中的有序节点，避免把榜单旁的推荐作品混进来。
    const auto data = kakuyomuPageData(html);
    bool ranking = false;
    const auto connection = kakuyomuListConnection(data, &ranking);
    if (connection.contains("nodes")) {
        const auto state = data.value("props").toObject().value("pageProps").toObject()
                               .value("__APOLLO_STATE__").toObject();
        static const QHash<QString, QString> genres = {
            {"FANTASY", QStringLiteral("異世界ファンタジー")},
            {"ACTION", QStringLiteral("現代ファンタジー")}, {"SF", "SF"},
            {"LOVE_STORY", QStringLiteral("恋愛")}, {"ROMANCE", QStringLiteral("ラブコメ")},
            {"DRAMA", QStringLiteral("現代ドラマ")}, {"HORROR", QStringLiteral("ホラー")},
            {"MYSTERY", QStringLiteral("ミステリー")},
            {"NONFICTION", QStringLiteral("エッセイ・ノンフィクション")},
            {"HISTORY", QStringLiteral("歴史・時代・伝奇")},
            {"CRITICISM", QStringLiteral("創作論・評論")},
            {"OTHERS", QStringLiteral("詩・童話・その他")},
            {"MAHO", QStringLiteral("魔法のiらんど")}, {"FAN_FICTION", QStringLiteral("二次創作")}
        };
        const int currentPage = qMax(1, data.value("query").toObject().value("page")
                                           .toVariant().toInt());
        int position = 0;
        for (const auto &node : connection.value("nodes").toArray()) {
            ++position;
            const auto work = state.value(node.toObject().value("__ref").toString()).toObject();
            const QString id = work.value("id").toString();
            if (id.isEmpty() || seen.contains(id) || work.value("title").toString().isEmpty())
                continue;
            seen.insert(id);
            const auto author = state.value(work.value("author").toObject()
                                                .value("__ref").toString()).toObject();
            QString authorName = work.value("alternateAuthorName").toString();
            if (authorName.isEmpty()) authorName = author.value("activityName").toString();
            if (authorName.isEmpty()) authorName = author.value("name").toString();
            const auto status = work.value("serialStatus").toString();
            QVariantMap card {
                {"workId", id}, {"title", work.value("title").toString()},
                {"author", authorName}, {"url", QStringLiteral("https://kakuyomu.jp/works/") + id},
                {"genre", genres.value(work.value("genre").toString(), work.value("genre").toString())},
                {"status", status == "RUNNING" ? QStringLiteral("連載中")
                           : status == "COMPLETED" ? QStringLiteral("完結済") : QString()},
                {"follow", QString::number(work.value("totalReviewPoint").toVariant().toLongLong())},
                {"chars", QString::number(work.value("totalCharacterCount").toVariant().toLongLong())},
                {"flagsKnown", work.contains("isCruel") && work.contains("isViolent") && work.contains("isSexual")},
                {"isCruel", work.value("isCruel").toBool()},
                {"isViolent", work.value("isViolent").toBool()},
                {"isSexual", work.value("isSexual").toBool()},
                {"hasPublication", work.value("hasPublication").toBool()},
                {"isNext", work.value("kakuyomuNextWork").isObject()}
            };
            if (ranking) card.insert("rank", (currentPage - 1) * 100 + position);
            cards.append(card);
        }
        return cards;
    }

    static const QRegularExpression linkRe(QStringLiteral("href=\"/works/(\\d+)\""));
    static const QRegularExpression authorRe(
        QStringLiteral("href=\"/users/[^\"]*\"[^>]*>([^<]+)</a>"));
    static const QRegularExpression followRe(QStringLiteral("★\\|([\\d,]+)"));
    static const QRegularExpression genreRe(QStringLiteral("★\\|[\\d,]+\\|([^|]+)\\|"));
    static const QRegularExpression statusRe(QStringLiteral("\\|(連載中|完結済|休載中)\\|"));
    static const QRegularExpression charRe(QStringLiteral("([\\d,]+)\\|?文字"));
    static const QRegularExpression tagRe(QStringLiteral("<[^>]+>"));
    static const QRegularExpression multiPipeRe(QStringLiteral("\\|{2,}"));

    // 页面内嵌的每本书标记（供「除外条件 / 抽出条件」本地筛选使用）
    const QVariantMap workFlags = parseKakuyomuWorkFlags(html);

    QRegularExpressionMatchIterator it = linkRe.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString id = m.captured(1);
        if (seen.contains(id))
            continue;
        const int start = qMax(0, m.capturedStart() - 500);
        const QString win = html.mid(start, 7000);
        const QRegularExpression titleRe(
            QStringLiteral("title=\"([^\"]*)\"\\s+href=\"/works/")
            + QRegularExpression::escape(id) + QStringLiteral("\""));
        const QRegularExpressionMatch tm = titleRe.match(win);
        if (!tm.hasMatch())
            continue;
        seen.insert(id);

        QVariantMap card;
        card.insert(QStringLiteral("workId"), id);
        card.insert(QStringLiteral("title"), decodeEntities(tm.captured(1)));
        card.insert(QStringLiteral("author"), decodeEntities(authorRe.match(win).captured(1)));
        card.insert(QStringLiteral("url"),
                    QStringLiteral("https://kakuyomu.jp/works/") + id);

        const QVariantMap flags = workFlags.value(id).toMap();
        card.insert(QStringLiteral("flagsKnown"), !flags.isEmpty());
        card.insert(QStringLiteral("isCruel"), flags.value(QStringLiteral("isCruel")).toBool());
        card.insert(QStringLiteral("isViolent"), flags.value(QStringLiteral("isViolent")).toBool());
        card.insert(QStringLiteral("isSexual"), flags.value(QStringLiteral("isSexual")).toBool());
        card.insert(QStringLiteral("hasPublication"),
                    flags.value(QStringLiteral("hasPublication")).toBool());
        card.insert(QStringLiteral("isNext"), flags.value(QStringLiteral("isNext")).toBool());

        // 卡片下方的元信息（★收藏 / 类型 / 连载状态 / 字数）
        const int metaIdx = win.indexOf(QStringLiteral("WorkMetaBasicInformation"));
        if (metaIdx >= 0) {
            QString meta = win.mid(metaIdx, 2500);
            meta.replace(tagRe, QStringLiteral("|"));
            meta.replace(multiPipeRe, QStringLiteral("|"));
            card.insert(QStringLiteral("follow"), followRe.match(meta).captured(1));
            const QString afterCount = genreRe.match(meta).captured(1);
            const QString status = statusRe.match(meta).captured(1);
            // 排行榜卡片不显示分类，紧跟收藏数的就是连载状态，别把它当分类
            card.insert(QStringLiteral("genre"),
                        (!afterCount.isEmpty() && afterCount == status) ? QString()
                                                                        : afterCount);
            card.insert(QStringLiteral("status"), status);
            card.insert(QStringLiteral("chars"), charRe.match(meta).captured(1));
        }
        cards.append(card);
    }
    return cards;
}

// 搜索 / 排行榜页把结果以 GraphQL(Apollo) 归一化数据内嵌在 __NEXT_DATA__ 中，
// 每本书都带 isCruel / isViolent / isSexual / hasPublication / kakuyomuNextWork。
// 站点本身不做服务端条件过滤（URL 参数会被忽略），所以这些标记用于本地筛选。
QVariantMap NovelParser::parseKakuyomuWorkFlags(const QString &html)
{
    QVariantMap flags;
    const QString key = QStringLiteral("\"Work:");
    int pos = 0;
    while ((pos = html.indexOf(key, pos)) >= 0) {
        pos += key.size();
        int p = pos;
        while (p < html.size() && html.at(p).isDigit())
            ++p;
        // 必须是 "Work:<数字>":{...}
        if (p == pos || p + 1 >= html.size()
            || html.at(p) != QLatin1Char('"') || html.at(p + 1) != QLatin1Char(':')) {
            pos = p;
            continue;
        }
        const QString id = html.mid(pos, p - pos);
        const int brace = p + 2;
        if (brace >= html.size() || html.at(brace) != QLatin1Char('{')) {
            pos = p;
            continue;
        }
        // 括号配对（跳过字符串字面量与转义）
        int depth = 0;
        bool inStr = false;
        bool esc = false;
        int end = -1;
        for (int i = brace; i < html.size(); ++i) {
            const QChar ch = html.at(i);
            if (inStr) {
                if (esc)
                    esc = false;
                else if (ch == QLatin1Char('\\'))
                    esc = true;
                else if (ch == QLatin1Char('"'))
                    inStr = false;
                continue;
            }
            if (ch == QLatin1Char('"')) {
                inStr = true;
            } else if (ch == QLatin1Char('{')) {
                ++depth;
            } else if (ch == QLatin1Char('}')) {
                if (--depth == 0) {
                    end = i;
                    break;
                }
            }
        }
        if (end < 0)
            break;
        const QJsonObject obj =
            QJsonDocument::fromJson(html.mid(brace, end - brace + 1).toUtf8()).object();
        if (!obj.isEmpty()) {
            QVariantMap f;
            f.insert(QStringLiteral("isCruel"),
                     obj.value(QStringLiteral("isCruel")).toBool());
            f.insert(QStringLiteral("isViolent"),
                     obj.value(QStringLiteral("isViolent")).toBool());
            f.insert(QStringLiteral("isSexual"),
                     obj.value(QStringLiteral("isSexual")).toBool());
            f.insert(QStringLiteral("hasPublication"),
                     obj.value(QStringLiteral("hasPublication")).toBool());
            f.insert(QStringLiteral("isNext"),
                     obj.value(QStringLiteral("kakuyomuNextWork")).isObject());
            flags.insert(id, f);
        }
        pos = end + 1;
    }
    return flags;
}

int NovelParser::parseKakuyomuPageCount(const QString &html)
{
    const auto data = kakuyomuPageData(html);
    bool ranking = false;
    const auto connection = kakuyomuListConnection(data, &ranking);
    if (!connection.isEmpty()) {
        const int currentPage = qMax(1, data.value("query").toObject().value("page")
                                           .toVariant().toInt());
        const bool hasNext = connection.value("pageInfo").toObject().value("hasNextPage").toBool();
        if (ranking) {
            // 站点榜单最多 500 名，每页 100；页信息只提供前后页，不虚构不存在的尾页。
            return qMin(5, currentPage + (hasNext ? 1 : 0));
        }
        if (connection.contains("totalCount"))
            return qMax(1, (connection.value("totalCount").toInt() + 19) / 20);
    }
    static const QRegularExpression re(QStringLiteral("page=(\\d+)"));
    int maxPage = 1;
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) {
        const int n = it.next().captured(1).toInt();
        if (n > maxPage)
            maxPage = n;
    }
    return maxPage;
}

QVariantMap NovelParser::parseKakuyomuWorkStats(const QString &html, const QString &workId)
{
    QVariantMap out;
    const QString needle = QStringLiteral("\"Work:%1\":{").arg(workId);
    const int hit = html.indexOf(needle);
    if (hit < 0)
        return out;
    const int objStart = html.indexOf(QLatin1Char('{'), hit);
    if (objStart < 0)
        return out;
    // 花括号配对，取出整个 Work 对象
    int depth = 0;
    int objEnd = -1;
    for (int k = objStart; k < html.size(); ++k) {
        const QChar ch = html.at(k);
        if (ch == QLatin1Char('{'))
            ++depth;
        else if (ch == QLatin1Char('}')) {
            --depth;
            if (depth == 0) { objEnd = k; break; }
        }
    }
    if (objEnd < 0)
        return out;
    const QString obj = html.mid(objStart, objEnd - objStart + 1);

    auto num = [&obj](const QString &key) {
        return QRegularExpression(QStringLiteral("\"%1\":(\\d+)").arg(key)).match(obj).captured(1);
    };
    auto str = [&obj](const QString &key) {
        const QString raw = QRegularExpression(
            QStringLiteral("\"%1\":\"((?:[^\"\\\\]|\\\\.)*)\"").arg(key)).match(obj).captured(1);
        return decodeJsonEscapes(raw);
    };

    out.insert(QStringLiteral("title"), str(QStringLiteral("title")));
    out.insert(QStringLiteral("catchphrase"), str(QStringLiteral("catchphrase")));
    // genre 是枚举（FANTASY / LOVE_STORY …），转成站点上的日文分类名
    const QString genreEnum = str(QStringLiteral("genre"));
    static const QHash<QString, QString> kGenreNames = {
        { QStringLiteral("FANTASY"),     QStringLiteral("奇幻") },
        { QStringLiteral("ACTION"),      QStringLiteral("动作") },
        { QStringLiteral("LOVE_STORY"),  QStringLiteral("恋爱") },
        { QStringLiteral("ROMANCE"),     QStringLiteral("恋爱") },
        { QStringLiteral("DRAMA"),       QStringLiteral("现代剧情") },
        { QStringLiteral("MYSTERY"),     QStringLiteral("悬疑") },
        { QStringLiteral("HORROR"),      QStringLiteral("恐怖") },
        { QStringLiteral("SF"),          QStringLiteral("科幻") },
        { QStringLiteral("HISTORY"),     QStringLiteral("历史·时代") },
        { QStringLiteral("NONFICTION"),  QStringLiteral("随笔·纪实") },
        { QStringLiteral("POETRY"),      QStringLiteral("诗歌") },
        { QStringLiteral("ESSAY"),       QStringLiteral("随笔") },
        { QStringLiteral("COMEDY"),      QStringLiteral("喜剧") },
    };
    out.insert(QStringLiteral("genre"), kGenreNames.value(genreEnum, genreEnum));
    const QString serial = str(QStringLiteral("serialStatus"));
    out.insert(QStringLiteral("status"),
               serial == QStringLiteral("COMPLETED") ? QStringLiteral("已完结")
                                                     : QStringLiteral("连载中"));
    out.insert(QStringLiteral("episodeCount"), num(QStringLiteral("publicEpisodeCount")));
    out.insert(QStringLiteral("charCount"), num(QStringLiteral("totalCharacterCount")));
    out.insert(QStringLiteral("reviewPoint"), num(QStringLiteral("totalReviewPoint")));
    out.insert(QStringLiteral("commentCount"), num(QStringLiteral("totalPublicEpisodeCommentCount")));
    out.insert(QStringLiteral("followCount"), num(QStringLiteral("totalFollowers")));
    out.insert(QStringLiteral("introduction"), str(QStringLiteral("introduction")));
    QVariantList flags;
    if (obj.contains(QStringLiteral("\"isCruel\":true")))
        flags << QStringLiteral("含残酷描写");
    if (obj.contains(QStringLiteral("\"isViolent\":true")))
        flags << QStringLiteral("含暴力描写");
    if (obj.contains(QStringLiteral("\"isSexual\":true")))
        flags << QStringLiteral("含性描写");
    out.insert(QStringLiteral("flags"), flags);
    out.insert(QStringLiteral("kind"),
               obj.contains(QStringLiteral("\"fanFictionSource\":null"))
                   ? QStringLiteral("原创小说") : QStringLiteral("二次创作"));
    QVariantList tags;
    const QRegularExpression tagsRe(QStringLiteral("\"tagLabels\":\\[([^\\]]*)\\]"));
    const QString tagsRaw = tagsRe.match(obj).captured(1);
    if (!tagsRaw.isEmpty()) {
        static const QRegularExpression tagItemRe(
            QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
        QRegularExpressionMatchIterator ti = tagItemRe.globalMatch(tagsRaw);
        while (ti.hasNext())
            tags << decodeJsonEscapes(ti.next().captured(1));
    }
    out.insert(QStringLiteral("tags"), tags);
    auto iso = [&obj](const QString &key) {
        return QRegularExpression(QStringLiteral("\"%1\":\"([^\"]+)\"").arg(key))
            .match(obj).captured(1);
    };
    auto toJst = [](const QString &isoText) {
        if (isoText.isEmpty())
            return QString();
        QDateTime dt = QDateTime::fromString(isoText, Qt::ISODate);
        if (!dt.isValid())
            return isoText;
        // 站点按访问者本地时区显示时间，这里保持一致
        return dt.toLocalTime().toString(QStringLiteral("yyyy年M月d日 HH:mm"));
    };
    out.insert(QStringLiteral("publishedAt"), toJst(iso(QStringLiteral("publishedAt"))));
    out.insert(QStringLiteral("updatedAt"),
               toJst(iso(QStringLiteral("lastEpisodePublishedAt"))));

    // 作者：Work.author 指向 UserAccount，取其 activityName
    // 作者：<title> 里的「作品名（作者名） - カクヨム」最可靠
    static const QRegularExpression titleAuthorRe(
        QStringLiteral("<title[^>]*>[^<]*（([^）]{1,40})）\\s*-\\s*カクヨム"));
    const QString author = titleAuthorRe.match(html).captured(1).trimmed();
    if (!author.isEmpty())
        out.insert(QStringLiteral("author"), decodeEntities(author));
    return out;
}

QVariantList NovelParser::parseKakuyomuComments(const QString &html)
{
    QVariantList list;
    const QJsonObject data = kakuyomuPageData(html);
    const QJsonObject state = data.value(QStringLiteral("props")).toObject()
                                  .value(QStringLiteral("pageProps")).toObject()
                                  .value(QStringLiteral("__APOLLO_STATE__")).toObject();
    QJsonObject connection;
    for (auto entity = state.constBegin(); entity != state.constEnd() && connection.isEmpty(); ++entity) {
        const QJsonObject object = entity.value().toObject();
        for (auto field = object.constBegin(); field != object.constEnd(); ++field) {
            if (field.key().startsWith(QStringLiteral("textualWorkReviews("))) {
                connection = field.value().toObject();
                break;
            }
        }
    }

    for (const QJsonValue &refValue : connection.value(QStringLiteral("nodes")).toArray()) {
        const QString ref = refValue.toObject().value(QStringLiteral("__ref")).toString();
        const QJsonObject review = state.value(ref).toObject();
        const QString body = review.value(QStringLiteral("body")).toString().trimmed();
        if (body.isEmpty())
            continue;
        const QString userRef = review.value(QStringLiteral("reviewerUser")).toObject()
                                    .value(QStringLiteral("__ref")).toString();
        const QJsonObject user = state.value(userRef).toObject();
        QString author = user.value(QStringLiteral("activityName")).toString();
        if (author.isEmpty())
            author = user.value(QStringLiteral("name")).toString();
        QString date = review.value(QStringLiteral("textualCreatedAt")).toString();
        if (date.size() >= 10)
            date = date.left(10);

        QVariantMap c;
        c.insert(QStringLiteral("id"), review.value(QStringLiteral("id")).toString());
        c.insert(QStringLiteral("title"), review.value(QStringLiteral("title")).toString());
        c.insert(QStringLiteral("body"), body);
        c.insert(QStringLiteral("isReply"), false);
        c.insert(QStringLiteral("author"), author);
        c.insert(QStringLiteral("date"), date);
        c.insert(QStringLiteral("isSpoiler"), review.value(QStringLiteral("isSpoiler")).toBool());
        c.insert(QStringLiteral("point"), review.value(QStringLiteral("point")).toInt());
        c.insert(QStringLiteral("likeCount"), review.value(QStringLiteral("likeCount")).toInt());
        list.append(c);
        if (list.size() >= 40)
            break;
    }
    return list;
}

QVariantList NovelParser::parseKakuyomuUsers(const QString &html)
{
    QVariantList users;
    const QJsonObject data = kakuyomuPageData(html);
    const QJsonObject state = data.value(QStringLiteral("props")).toObject()
                                  .value(QStringLiteral("pageProps")).toObject()
                                  .value(QStringLiteral("__APOLLO_STATE__")).toObject();

    QJsonObject connection;
    for (auto entity = state.constBegin(); entity != state.constEnd() && connection.isEmpty(); ++entity) {
        const QJsonObject object = entity.value().toObject();
        for (auto field = object.constBegin(); field != object.constEnd(); ++field) {
            if (field.key().startsWith(QStringLiteral("searchUsers("))) {
                connection = field.value().toObject();
                break;
            }
        }
    }

    for (const QJsonValue &node : connection.value(QStringLiteral("nodes")).toArray()) {
        const QString ref = node.toObject().value(QStringLiteral("__ref")).toString();
        const QJsonObject account = state.value(ref).toObject();
        QString screenName = account.value(QStringLiteral("screenName")).toString();
        if (screenName.startsWith(QLatin1Char('@')))
            screenName.remove(0, 1);
        const QString name = account.value(QStringLiteral("name")).toString();
        const QString activityName = account.value(QStringLiteral("activityName")).toString();
        if (screenName.isEmpty() && name.isEmpty())
            continue;
        users.append(QVariantMap{
            {QStringLiteral("screenName"), screenName},
            {QStringLiteral("name"), name},
            {QStringLiteral("activityName"), activityName},
            {QStringLiteral("profileText"), account.value(QStringLiteral("profileText")).toString()},
            {QStringLiteral("workCount"), account.value(QStringLiteral("totalPublicWorks")).toInt()},
            {QStringLiteral("url"), QStringLiteral("https://kakuyomu.jp/users/") + screenName},
        });
    }
    return users;
}

QVariantList NovelParser::parseKakuyomuAuthorWorks(const QString &html)
{
    QVariantList cards;
    const QJsonObject data = kakuyomuPageData(html);
    const QJsonObject state = data.value(QStringLiteral("props")).toObject()
                                  .value(QStringLiteral("pageProps")).toObject()
                                  .value(QStringLiteral("__APOLLO_STATE__")).toObject();

    QJsonObject connection;
    for (auto entity = state.constBegin(); entity != state.constEnd() && connection.isEmpty(); ++entity) {
        const QJsonObject object = entity.value().toObject();
        for (auto field = object.constBegin(); field != object.constEnd(); ++field) {
            if (field.key().startsWith(QStringLiteral("worksInLastEpisodePublishedOrder("))) {
                connection = field.value().toObject();
                break;
            }
        }
    }

    static const QHash<QString, QString> genres = {
        {"FANTASY", QStringLiteral("異世界ファンタジー")},
        {"ACTION", QStringLiteral("現代ファンタジー")}, {"SF", "SF"},
        {"LOVE_STORY", QStringLiteral("恋愛")}, {"ROMANCE", QStringLiteral("ラブコメ")},
        {"DRAMA", QStringLiteral("現代ドラマ")}, {"HORROR", QStringLiteral("ホラー")},
        {"MYSTERY", QStringLiteral("ミステリー")},
        {"NONFICTION", QStringLiteral("エッセイ・ノンフィクション")},
        {"HISTORY", QStringLiteral("歴史・時代・伝奇")},
        {"CRITICISM", QStringLiteral("創作論・評論")},
        {"OTHERS", QStringLiteral("詩・童話・その他")},
        {"MAHO", QStringLiteral("魔法のiらんど")}, {"FAN_FICTION", QStringLiteral("二次創作")}
    };

    QSet<QString> seen;
    for (const QJsonValue &node : connection.value(QStringLiteral("nodes")).toArray()) {
        const QString ref = node.toObject().value(QStringLiteral("__ref")).toString();
        const QJsonObject work = state.value(ref).toObject();
        const QString id = work.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || seen.contains(id) || work.value(QStringLiteral("title")).toString().isEmpty())
            continue;
        seen.insert(id);
        const QJsonObject author = state.value(work.value(QStringLiteral("author")).toObject()
                                                   .value(QStringLiteral("__ref")).toString()).toObject();
        QString authorName = work.value(QStringLiteral("alternateAuthorName")).toString();
        if (authorName.isEmpty())
            authorName = author.value(QStringLiteral("activityName")).toString();
        if (authorName.isEmpty())
            authorName = author.value(QStringLiteral("name")).toString();
        const QString status = work.value(QStringLiteral("serialStatus")).toString();
        cards.append(QVariantMap{
            {QStringLiteral("workId"), id},
            {QStringLiteral("title"), work.value(QStringLiteral("title")).toString()},
            {QStringLiteral("author"), authorName},
            {QStringLiteral("url"), QStringLiteral("https://kakuyomu.jp/works/") + id},
            {QStringLiteral("genre"), genres.value(work.value(QStringLiteral("genre")).toString(),
                                                   work.value(QStringLiteral("genre")).toString())},
            {QStringLiteral("status"), status == QStringLiteral("RUNNING") ? QStringLiteral("連載中")
                           : status == QStringLiteral("COMPLETED") ? QStringLiteral("完結済") : QString()},
            {QStringLiteral("follow"), QString::number(work.value(QStringLiteral("totalReviewPoint")).toVariant().toLongLong())},
            {QStringLiteral("chars"), QString::number(work.value(QStringLiteral("totalCharacterCount")).toVariant().toLongLong())},
            {QStringLiteral("flagsKnown"), work.contains(QStringLiteral("isCruel"))
                                              && work.contains(QStringLiteral("isViolent"))
                                              && work.contains(QStringLiteral("isSexual"))},
            {QStringLiteral("isCruel"), work.value(QStringLiteral("isCruel")).toBool()},
            {QStringLiteral("isViolent"), work.value(QStringLiteral("isViolent")).toBool()},
            {QStringLiteral("isSexual"), work.value(QStringLiteral("isSexual")).toBool()},
            {QStringLiteral("hasPublication"), work.value(QStringLiteral("hasPublication")).toBool()},
            {QStringLiteral("isNext"), work.value(QStringLiteral("kakuyomuNextWork")).isObject()},
        });
    }
    return cards;
}
