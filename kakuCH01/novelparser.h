#ifndef NOVELPARSER_H
#define NOVELPARSER_H

#include "novelwork.h"
#include <QString>
#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>

// 按站点分派的解析入口。各站点的页面结构不同，这里统一对外提供
// 「作品页 -> NovelWork」「章节页 -> 正文纯文本」两个能力。
class NovelParser
{
public:
    // siteKey 见 NovelPaths；未支持的站点返回空 NovelWork / 空正文
    static NovelWork parseWorkPage(const QString &siteKey, const QString &html,
                                   const QString &workUrl);
    static QString parseEpisodeBody(const QString &siteKey, const QString &html);

    // 该站点是否已实现解析
    static bool supportsParsing(const QString &siteKey);
    // URL-encoded Pink age-confirmation form; only accepts the same-site form.
    static QByteArray solispiaAgeConfirmationForm(const QString &html, const QString &returnUrl);
    // Paginated public catalogues must be merged before emitting workReady.
    static QString nextWorkPage(const QString &siteKey, const QString &html, const QString &pageUrl);

    // 共用工具
    static QString decodeEntities(const QString &text);
    static QString sanitizeFileName(const QString &name);
    static QString stripHtml(const QString &fragment);
    // 处理 JSON 里的 \uXXXX 等转义
    static QString decodeJsonEscapes(const QString &text);

    // ---- 「网站」页用的カクヨム浏览解析 ----
    static QVariantList parseKakuyomuCards(const QString &html);
    // 从搜索/排行榜页内嵌的 __NEXT_DATA__ 里取出每本书的标记：
    // 残酷描写 / 暴力描写 / 性描写 / 已出版 / NEXT 刊登（用于本地筛选条件）
    static QVariantMap parseKakuyomuWorkFlags(const QString &html);
    static QVariantMap parseKakuyomuWorkStats(const QString &html, const QString &workId);
    static int parseKakuyomuPageCount(const QString &html);
    static QVariantList parseKakuyomuComments(const QString &html);
    static QVariantList parseKakuyomuUsers(const QString &html);
    static QVariantList parseKakuyomuAuthorWorks(const QString &html);
};

#endif // NOVELPARSER_H
