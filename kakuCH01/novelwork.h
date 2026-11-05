#ifndef NOVELWORK_H
#define NOVELWORK_H

#include "chapter.h"
#include <QString>
#include <QVector>
#include <QDir>
#include <QFileInfo>

// 一部小说的数据结构
struct NovelWork
{
    QString title;   // 小说标题
    QString author;  // 作者名（可能为空）
    QString workId;  // 书籍编号
    QString workUrl; // 规范化后的作品页地址
    QString siteKey; // 站点标识（kakuyomujp / novelupplus / ...），用于隔离目录
    QString rootPath; // 小说目录（DOWNLOAD/<siteKey>/<workId>），下载前由业务层填充
    QVector<Chapter> chapters;

    bool isValid() const { return !workId.isEmpty(); }

    // 目录约定：novel.json / chapters/ 均位于 rootPath 下
    QString novelJsonPath() const { return QDir(rootPath).filePath(QStringLiteral("novel.json")); }
    QString chaptersPath() const { return QDir(rootPath).filePath(QStringLiteral("chapters")); }
};

#endif // NOVELWORK_H
