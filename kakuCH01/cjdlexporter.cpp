#include "cjdlexporter.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace {
constexpr int kCjdlVersion = 1;
}

QString CjdlExporter::cjdlPathFor(const QString &cjdlDir,
                                  const QString &workId,
                                  const QString &chapterId)
{
    return QDir(QDir(cjdlDir).filePath(workId)).filePath(
        QStringLiteral("chapters") + QStringLiteral("/") + chapterId + QStringLiteral(".json"));
}

bool CjdlExporter::write(const QString &cjdlDir,
                         const QString &workId,
                         const QString &chapterId,
                         const QString &title,
                         const QList<CjdlBlock> &blocks)
{
    if (cjdlDir.isEmpty() || workId.isEmpty() || chapterId.isEmpty())
        return false;

    QDir chaptersDir(QDir(QDir(cjdlDir).filePath(workId)).filePath(QStringLiteral("chapters")));
    if (!chaptersDir.exists() && !chaptersDir.mkpath(QStringLiteral(".")))
        return false;

    QJsonArray blockArray;
    for (int i = 0; i < blocks.size(); ++i) {
        QJsonObject b;
        b.insert(QStringLiteral("index"), blocks[i].index >= 0 ? blocks[i].index : i);
        b.insert(QStringLiteral("source"), blocks[i].source);
        b.insert(QStringLiteral("translation"), blocks[i].translation);
        blockArray.append(b);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kCjdlVersion);
    root.insert(QStringLiteral("workId"), workId);
    root.insert(QStringLiteral("chapterId"), chapterId);
    root.insert(QStringLiteral("title"), title);
    root.insert(QStringLiteral("blocks"), blockArray);

    const QString path = chaptersDir.filePath(chapterId + QStringLiteral(".json"));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        qWarning() << "[CJDL] 保存失败:" << path;
        return false;
    }
    return true;
}

bool CjdlExporter::read(const QString &path,
                        QList<CjdlBlock> &blocks,
                        QString &workId,
                        QString &chapterId,
                        QString &title)
{
    blocks.clear();
    workId.clear();
    chapterId.clear();
    title.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    workId = root.value(QStringLiteral("workId")).toString();
    chapterId = root.value(QStringLiteral("chapterId")).toString();
    title = root.value(QStringLiteral("title")).toString();

    const QJsonArray arr = root.value(QStringLiteral("blocks")).toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject b = v.toObject();
        CjdlBlock block;
        block.index = b.value(QStringLiteral("index")).toInt();
        block.source = b.value(QStringLiteral("source")).toString();
        block.translation = b.value(QStringLiteral("translation")).toString();
        blocks.append(block);
    }
    return true;
}
