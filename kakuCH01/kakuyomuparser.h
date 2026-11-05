#ifndef KAKUYOMUPARSER_H
#define KAKUYOMUPARSER_H

#include "novelwork.h"
#include <QString>

// 负责把 kakuyomu 的 HTML 解析为结构化数据
class KakuyomuParser
{
public:
    // 解析作品页，得到标题与全部章节
    static NovelWork parseWorkPage(const QString &html, const QString &workUrl);
    // 解析章节页，得到正文纯文本
    static QString parseEpisodeBody(const QString &html);
    // HTML 实体解码
    static QString decodeEntities(const QString &text);
    // 过滤文件名中的非法字符
    static QString sanitizeFileName(const QString &name);

private:
    // 将一段 HTML 片段转为纯文本（处理 ruby 注音、br 等）
    static QString stripHtml(const QString &fragment);
};

#endif // KAKUYOMUPARSER_H