#include "novelstore.h"
#include "novelpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

QString NovelStore::ensureNovelDirectory(const NovelWork &work, const QString &downloadRoot)
{
    if (work.workId.isEmpty())
        return QString();

    // 目录布局：DOWNLOAD/<siteKey>/<workId>
    // siteKey 缺失时（老数据）退回旧的 DOWNLOAD/<workId>，避免老书找不到自己的目录。
    const QString root = NovelPaths::novelRoot(downloadRoot, work.siteKey, work.workId);
    QDir d(root);
    if (!d.exists() && !d.mkpath(QStringLiteral(".")))
        return QString();
    d.mkpath(QStringLiteral("chapters"));

    // 读取已有 novel.json，保留未识别字段，再更新已知字段
    QJsonObject obj;
    QString novelJson = d.filePath(QStringLiteral("novel.json"));
    QFile rf(novelJson);
    if (rf.exists() && rf.open(QIODevice::ReadOnly)) {
        QJsonDocument old = QJsonDocument::fromJson(rf.readAll());
        rf.close();
        if (old.isObject())
            obj = old.object();
    }

    obj.insert(QStringLiteral("workId"), work.workId);
    obj.insert(QStringLiteral("url"), work.workUrl);
    if (!work.siteKey.isEmpty())
        obj.insert(QStringLiteral("site"), work.siteKey);
    if (!work.title.isEmpty())
        obj.insert(QStringLiteral("title"), work.title);
    if (!work.author.isEmpty())
        obj.insert(QStringLiteral("author"), work.author);

    QJsonArray chapters;
    for (const Chapter &ch : work.chapters) {
        QJsonObject c;
        c.insert(QStringLiteral("number"), ch.number);
        c.insert(QStringLiteral("title"), ch.title);
        c.insert(QStringLiteral("episodeId"), ch.episodeId);
        c.insert(QStringLiteral("episodeUrl"), ch.episodeUrl);
        chapters.append(c);
    }
    obj.insert(QStringLiteral("chapters"), chapters);

    QSaveFile wf(novelJson);
    if (wf.open(QIODevice::WriteOnly)) {
        wf.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        wf.commit();
    }

    return root;
}

NovelWork NovelStore::loadNovel(const QString &rootPath)
{
    NovelWork work;
    work.rootPath = rootPath;

    QFile f(QDir(rootPath).filePath(QStringLiteral("novel.json")));
    if (!f.open(QIODevice::ReadOnly))
        return work;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return work;

    QJsonObject obj = doc.object();
    work.title = obj.value(QStringLiteral("title")).toString();
    work.author = obj.value(QStringLiteral("author")).toString();
    work.workId = obj.value(QStringLiteral("workId")).toString();
    work.workUrl = obj.value(QStringLiteral("url")).toString();
    work.siteKey = obj.value(QStringLiteral("site")).toString();

    const QJsonArray chapters = obj.value(QStringLiteral("chapters")).toArray();
    for (const QJsonValue &v : chapters) {
        if (!v.isObject())
            continue;
        QJsonObject c = v.toObject();
        Chapter ch;
        ch.title = c.value(QStringLiteral("title")).toString();
        ch.episodeId = c.value(QStringLiteral("episodeId")).toString();
        ch.episodeUrl = c.value(QStringLiteral("episodeUrl")).toString();
        ch.number = c.value(QStringLiteral("number")).toInt();
        work.chapters.append(ch);
    }
    return work;
}
