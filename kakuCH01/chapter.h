#ifndef CHAPTER_H
#define CHAPTER_H

#include <QString>

// 单个章节的数据结构
struct Chapter
{
    QString title;      // 例: 第1话
    QString episodeId;  // 例: 16817330669719363977
    QString episodeUrl; // 例: https://kakuyomu.jp/works/.../episodes/...
    int number = 0;     // 在整本书中的稳定序号（1 起），用于生成章节文件名

    bool isValid() const { return !episodeId.isEmpty(); }
};

#endif // CHAPTER_H
