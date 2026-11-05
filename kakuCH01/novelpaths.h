#ifndef NOVELPATHS_H
#define NOVELPATHS_H

#include <QString>

// 站点识别与目录布局工具。
//
// 目录约定（DOWNLOAD 根下）：
//   DOWNLOAD/<siteKey>/<workId>/            —— 新布局，siteKey 用来隔离不同站点
//   DOWNLOAD/<workId>/                      —— 旧布局（只支持 kakuyomu 时期产生的），读取时兼容
//
// siteKey = 作品页域名去掉开头的 www. 后，仅保留字母与数字：
//   kakuyomu.jp            -> kakuyomujp
//   novelup.plus           -> novelupplus
//   novel.daysneo.com      -> noveldays
//   novelba.com            -> novelba (indies works)
//   solispia.com           -> solispia (title URLs)
//   pink.solispia.com      -> solispiapink (separate from the main site)
//   www.alphapolis.co.jp   -> alphapoliscopjp
//   estar.jp               -> estarjp
//   syosetu.org            -> syosetuorg
//   pixiv.net              -> pixivnet（单篇数字 ID；系列为 series_<id>）
//
// 译文/双语资源目录与原文目录同构，只是根目录换成 CHDL / CJDL：
//   CHDL/<siteKey>/<workId>/chapters/
//   CJDL/<siteKey>/<workId>/chapters/
class NovelPaths
{
public:
    // URL -> 域名（小写、去掉端口与开头的 www.）
    static QString hostOf(const QString &url);
    // 域名 -> siteKey
    static QString siteKeyFromHost(const QString &host);
    // URL -> siteKey（无法识别时返回空）
    static QString siteKeyFromUrl(const QString &url);

    // 是否属于当前支持的站点
    static bool isSupportedSite(const QString &host);
    // 站点显示名（用于界面提示）
    static QString siteDisplayName(const QString &siteKey);

    // 作品页地址 -> workId；失败时把原因写入 error
    static QString workIdFromUrl(const QString &url, QString *error = nullptr);
    // 把任意输入网址规范化为作品页地址
    static QString canonicalWorkUrl(const QString &url);
    // siteKey + workId -> 作品页地址（无法只凭 workId 复原的站点返回空，例如 alphapolis）
    static QString workUrlFor(const QString &siteKey, const QString &workId);

    // 新布局：DOWNLOAD/<siteKey>/<workId>
    static QString novelRoot(const QString &downloadRoot, const QString &siteKey,
                             const QString &workId);
    // 旧布局：DOWNLOAD/<workId>
    static QString legacyNovelRoot(const QString &downloadRoot, const QString &workId);

    // 由小说根目录推出「相对标识」：
    //   新布局 -> "<siteKey>/<workId>"，旧布局 -> "<workId>"
    // 之后把它拼到 CHDL / CJDL 根下即可得到对应目录，新旧布局都能对上。
    static QString relativeKey(const QString &downloadRoot, const QString &novelRoot);

    static QString translationChaptersDir(const QString &chdlRoot, const QString &relativeKey);
    static QString bilingualChaptersDir(const QString &cjdlRoot, const QString &relativeKey);
    static QString bilingualChapterFile(const QString &cjdlRoot, const QString &relativeKey,
                                        const QString &chapterBaseName);
};

#endif // NOVELPATHS_H
