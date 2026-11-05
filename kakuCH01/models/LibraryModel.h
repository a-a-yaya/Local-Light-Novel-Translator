#ifndef LIBRARYMODEL_H
#define LIBRARYMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QStringList>

// 书库模型：扫描下载目录与译文目录，聚合出每本小说。
// 同时兼容新结构 <workId>/chapters/*.txt 与旧扁平结构 “标题 - 章节.txt”。
class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        AuthorRole,
        ChapterCountRole,
        TranslatedCountRole,
        DownloadedCountRole,
        ProgressRole,
        CoverPathRole,
        SourceDirRole,
        TranslationDirRole,
        NovelIdRole,
        SiteRole,       // 站点标识（kakuyomujp / novelupplus / ...）
        SiteNameRole,   // 站点显示名（カクヨム / ノベルアップ＋ / ...）
        ReadCountRole   // 已阅读（中文目录）章节数
    };
    Q_ENUM(Roles)

    explicit LibraryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDirectories(const QString &downloadDir, const QString &translationDir);
    void refresh();
    Q_INVOKABLE void setFilter(const QString &filter);
    Q_INVOKABLE int sourceRow(int displayRow) const;

    int totalNovelCount() const { return m_novels.size(); }
    int totalDownloadedCount() const;
    int totalTranslatedCount() const;

    Q_INVOKABLE QStringList novelChapterNames(int row) const;
    Q_INVOKABLE QString novelChapterName(int row, int chapterIndex) const;
    Q_INVOKABLE QString novelChapterTitle(int row, int chapterIndex) const;
    Q_INVOKABLE bool novelChapterTranslated(int row, int chapterIndex) const;
    Q_INVOKABLE QString novelSourceDir(int row) const;
    Q_INVOKABLE QString novelTranslationDir(int row) const;
    Q_INVOKABLE QString novelTitle(int row) const;
    Q_INVOKABLE QString novelAuthor(int row) const;
    Q_INVOKABLE QString novelWorkId(int row) const;
    Q_INVOKABLE QString novelSite(int row) const;
    Q_INVOKABLE QString novelSiteName(int row) const;
    Q_INVOKABLE QString novelRootPath(int row) const;
    Q_INVOKABLE int novelChapterCount(int row) const;
    Q_INVOKABLE int novelTranslatedCount(int row) const;
    Q_INVOKABLE int novelDownloadedCount(int row) const;

private:
    void rebuildFilter();
    struct Novel
    {
        QString title;
        QString author;
        QString workId;
        QString siteKey;       // 所属站点标识
        QString rootPath;      // 新结构的小说根目录；旧结构为空
        QString sourceDir;     // 原文章节目录
        QString translationDir;
        QString legacyPrefix;  // 旧扁平结构的小说名前缀；新结构为空
        QStringList chapterNames; // 刷新时扫描一次，目录显示复用同一份快照
        bool readChapterHeaders = false;
        mutable QHash<QString, QString> chapterTitles; // 章节文件 basename -> 标题
        int chapterCount = 0;
        int translatedCount = 0;
        int readCount = 0;      // 已阅读（中文目录）章节数
    };

    QVector<Novel> m_novels;
    QString m_downloadDir;
    QString m_translationDir;
    QString m_filter;
    QVector<int> m_filteredIndex;
};

#endif // LIBRARYMODEL_H
