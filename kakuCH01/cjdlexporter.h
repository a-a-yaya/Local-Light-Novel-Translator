#ifndef CJDLEXPORTER_H
#define CJDLEXPORTER_H

#include <QString>
#include <QList>

// 中日双语阅读资源的一个显示块：一个 translation chunk 对应一个 block。
struct CjdlBlock
{
    int index = 0;
    QString source;
    QString translation;
};

// CJDL/<workId>/chapters/<chapterId>.json 的读写。
class CjdlExporter
{
public:
    static QString cjdlPathFor(const QString &cjdlDir,
                               const QString &workId,
                               const QString &chapterId);

    static bool write(const QString &cjdlDir,
                      const QString &workId,
                      const QString &chapterId,
                      const QString &title,
                      const QList<CjdlBlock> &blocks);

    static bool read(const QString &path,
                     QList<CjdlBlock> &blocks,
                     QString &workId,
                     QString &chapterId,
                     QString &title);
};

#endif // CJDLEXPORTER_H
