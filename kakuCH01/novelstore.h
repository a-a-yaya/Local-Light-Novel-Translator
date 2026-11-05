#ifndef NOVELSTORE_H
#define NOVELSTORE_H

#include "novelwork.h"
#include <QString>

// 小说目录持久化：负责 novel.json 的读写与目录初始化。
class NovelStore
{
public:
    // 在 downloadRoot/<workId> 下确保 novel.json、chapters/ 存在
    // 返回小说根目录；失败返回空串
    static QString ensureNovelDirectory(const NovelWork &work, const QString &downloadRoot);

    // 从 <rootPath>/novel.json 读取元数据（含 rootPath）
    static NovelWork loadNovel(const QString &rootPath);
};

#endif // NOVELSTORE_H
