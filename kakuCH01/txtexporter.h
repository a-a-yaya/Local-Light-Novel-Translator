#ifndef TXTEXPORTER_H
#define TXTEXPORTER_H

#include "chapter.h"
#include <QString>

// 负责把章节正文写入 txt 文件
class TxtExporter
{
public:
    // 将单个章节导出为 txt，返回完整文件路径；失败返回空串并填充 errorMessage
    static QString exportChapter(const QString &dir, const QString &workTitle,
                                 const Chapter &chapter, const QString &body,
                                 QString *errorMessage = nullptr);
    // 将单个章节导出到指定绝对文件路径（新目录结构使用）
    static QString exportChapterToFile(const QString &filePath, const QString &workTitle,
                                       const Chapter &chapter, const QString &body,
                                       QString *errorMessage = nullptr);
    // 只识别下载器写出的完整头部：第一行「作品 - 章节」，第二行来源网址。
    // 缺少任一特征时整份内容都按正文处理，避免误删正常正文。
    static void splitChapterDocument(const QString &text, QString *header, QString *body);
    static QString mergeChapterDocument(const QString &header, const QString &body);
    static QString chapterTitleFromDocument(const QString &text);
};

#endif // TXTEXPORTER_H
