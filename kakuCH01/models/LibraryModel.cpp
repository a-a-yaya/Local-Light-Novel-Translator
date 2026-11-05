#include "LibraryModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include "novelstore.h"
#include "novelpaths.h"
#include "txtexporter.h"

LibraryModel::LibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_filteredIndex.size();
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return QVariant();

    const int real = m_filteredIndex[index.row()];
    const Novel &n = m_novels[real];
    switch (role) {
    case TitleRole:
        return n.title;
    case AuthorRole:
        return n.author;
    case ChapterCountRole:
        return n.chapterCount;
    case TranslatedCountRole:
        return n.translatedCount;
    case DownloadedCountRole:
        return n.chapterCount;
    case ProgressRole:
        if (n.chapterCount == 0)
            return 0.0;
        return double(n.translatedCount) / double(n.chapterCount);
    case CoverPathRole:
        return QString();
    case SourceDirRole:
        return n.sourceDir;
    case TranslationDirRole:
        return n.translationDir;
    case NovelIdRole:
        return n.workId.isEmpty() ? n.title : n.workId;
    case SiteRole:
        return n.siteKey;
    case SiteNameRole:
        return n.siteKey.isEmpty() ? QString() : NovelPaths::siteDisplayName(n.siteKey);
    case ReadCountRole:
        return n.readCount;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[TitleRole] = "title";
    roles[AuthorRole] = "author";
    roles[ChapterCountRole] = "chapterCount";
    roles[TranslatedCountRole] = "translatedCount";
    roles[DownloadedCountRole] = "downloadedCount";
    roles[ProgressRole] = "progress";
    roles[CoverPathRole] = "coverPath";
    roles[SourceDirRole] = "sourceDir";
    roles[TranslationDirRole] = "translationDir";
    roles[NovelIdRole] = "novelId";
    roles[SiteRole] = "site";
    roles[SiteNameRole] = "siteName";
    roles[ReadCountRole] = "readCount";
    return roles;
}

void LibraryModel::setDirectories(const QString &downloadDir, const QString &translationDir)
{
    m_downloadDir = downloadDir;
    m_translationDir = translationDir;
    refresh();
}

void LibraryModel::rebuildFilter()
{
    m_filteredIndex.clear();
    for (int i = 0; i < m_novels.size(); ++i) {
        if (m_filter.isEmpty() || m_novels[i].title.contains(m_filter, Qt::CaseInsensitive))
            m_filteredIndex.append(i);
    }
}

void LibraryModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    beginResetModel();
    rebuildFilter();
    endResetModel();
}

int LibraryModel::sourceRow(int displayRow) const
{
    return (displayRow >= 0 && displayRow < m_filteredIndex.size())
               ? m_filteredIndex[displayRow]
               : -1;
}

int LibraryModel::totalDownloadedCount() const
{
    int total = 0;
    for (const Novel &n : m_novels)
        total += n.chapterCount;
    return total;
}

int LibraryModel::totalTranslatedCount() const
{
    int total = 0;
    for (const Novel &n : m_novels)
        total += n.translatedCount;
    return total;
}

void LibraryModel::refresh()
{
    beginResetModel();
    m_novels.clear();

    QDir dl(m_downloadDir);
    QDir td(m_translationDir);
    QSet<QString> usedTitles; // 避免旧扁平聚合与子目录标题重复

    // 收集所有「小说根目录」：<siteKey>/<workId>（新）与 <workId>（旧），
    // 两者都以「存在 chapters/ 子目录」为判据。
    auto collectNovel = [&](const QString &rootPath, const QString &fallbackName) {
        const QString chaptersDir = QDir(rootPath).filePath(QStringLiteral("chapters"));
        const QStringList files =
            QDir(chaptersDir).entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files);
        if (files.isEmpty())
            return;
        const QString rel = NovelPaths::relativeKey(m_downloadDir, rootPath);
        Novel n;
        NovelWork meta = NovelStore::loadNovel(rootPath);
        n.title = meta.title.isEmpty() ? fallbackName : meta.title;
        n.author = meta.author;
        n.workId = meta.workId.isEmpty() ? QDir(rootPath).dirName() : meta.workId;
        // 站点：优先取 novel.json 里记录的，其次从目录层级反推
        // （<siteKey>/<workId> 取第一层；旧单层 <workId> 视为カクヨム时期的数据）
        n.siteKey = meta.siteKey;
        if (n.siteKey.isEmpty()) {
            n.siteKey = rel.contains(QLatin1Char('/'))
                            ? rel.section(QLatin1Char('/'), 0, 0)
                            : QStringLiteral("kakuyomujp");
        }
        n.rootPath = rootPath;
        n.sourceDir = chaptersDir;
        n.translationDir = NovelPaths::translationChaptersDir(m_translationDir, rel);
        for (const Chapter &chapter : meta.chapters) {
            const QString base =
                QString::number(chapter.number).rightJustified(3, QLatin1Char('0'));
            if (!base.isEmpty() && !chapter.title.trimmed().isEmpty())
                n.chapterTitles.insert(base, chapter.title.trimmed());
        }
        n.readChapterHeaders = meta.chapters.isEmpty();
        for (const QString &file : files)
            n.chapterNames.append(QFileInfo(file).completeBaseName());
        n.chapterCount = files.size();
        n.translatedCount = QDir(n.translationDir).entryList(
                                QStringList() << QStringLiteral("*.txt"), QDir::Files).size();
        m_novels.append(n);
        usedTitles.insert(n.title);
    };

    const QStringList subdirs = dl.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subdirs) {
        const QString dirPath = dl.filePath(sub);

        // 1) 旧布局：<workId>/chapters/*.txt
        if (QDir(QDir(dirPath).filePath(QStringLiteral("chapters"))).exists()) {
            collectNovel(dirPath, sub);
            continue;
        }

        // 2) 新布局：<siteKey>/<workId>/chapters/*.txt
        const QDir sd(dirPath);
        const QStringList siteSubs = sd.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        bool foundNested = false;
        for (const QString &siteSub : siteSubs) {
            const QString nested = sd.filePath(siteSub);
            if (!QDir(QDir(nested).filePath(QStringLiteral("chapters"))).exists())
                continue;
            collectNovel(nested, siteSub);
            foundNested = true;
        }
        if (foundNested)
            continue;

        // 3) 旧「子目录直接放 txt」格式
        const QStringList txts = sd.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files);
        if (!txts.isEmpty()) {
            Novel n;
            n.title = sub;
            n.sourceDir = dirPath;
            const QString transSub = td.filePath(sub);
            n.translationDir = QDir(transSub).exists() ? transSub : m_translationDir;
            for (const QString &file : txts)
                n.chapterNames.append(QFileInfo(file).completeBaseName());
            n.chapterCount = txts.size();
            n.translatedCount = QDir(n.translationDir).entryList(
                                    QStringList() << QStringLiteral("*.txt"), QDir::Files).size();
            m_novels.append(n);
            usedTitles.insert(n.title);
        }
    }

    // 4) 旧扁平结构：下载目录根下的 “标题 - 章节.txt”
    QStringList rootTxts = dl.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files);
    QHash<QString, QStringList> byPrefix;
    for (const QString &f : rootTxts) {
        int sep = f.indexOf(QStringLiteral(" - "));
        QString prefix = (sep > 0) ? f.left(sep) : QStringLiteral("未分类");
        byPrefix[prefix].append(f);
    }
    QHash<QString, int> translatedByPrefix;
    const QStringList translatedFiles = td.entryList({QStringLiteral("*.txt")}, QDir::Files);
    for (const QString &file : translatedFiles) {
        const int separator = file.indexOf(QStringLiteral(" - "));
        ++translatedByPrefix[separator > 0 ? file.left(separator) : QStringLiteral("未分类")];
    }
    for (auto it = byPrefix.begin(); it != byPrefix.end(); ++it) {
        if (usedTitles.contains(it.key()))
            continue;
        Novel n;
        n.title = it.key();
        n.sourceDir = m_downloadDir;
        n.translationDir = m_translationDir;
        n.legacyPrefix = it.key();
        n.chapterCount = it.value().size();
        for (const QString &file : it.value())
            n.chapterNames.append(QFileInfo(file).completeBaseName());
        n.translatedCount = translatedByPrefix.value(it.key());
        m_novels.append(n);
    }

    // 已阅读数：与阅读器一致，读 QSettings 里该小说的 zhRead（中文目录已读章节）列表
    {
        QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
        for (int i = 0; i < m_novels.size(); ++i) {
            const QString key = m_novels[i].workId.isEmpty() ? m_novels[i].title
                                                             : m_novels[i].workId;
            if (key.isEmpty()) {
                m_novels[i].readCount = 0;
                continue;
            }
            settings.beginGroup(QStringLiteral("reading/") + key);
            const QStringList zhRead =
                settings.value(QStringLiteral("zhRead")).toStringList();
            settings.endGroup();
            m_novels[i].readCount = qMin(zhRead.size(), m_novels[i].chapterCount);
        }
    }

    rebuildFilter();
    endResetModel();
}

QString LibraryModel::novelSourceDir(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].sourceDir : QString();
}

QString LibraryModel::novelTranslationDir(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].translationDir : QString();
}

QString LibraryModel::novelTitle(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].title : QString();
}

QString LibraryModel::novelAuthor(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].author : QString();
}

QString LibraryModel::novelWorkId(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].workId : QString();
}

QString LibraryModel::novelSite(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].siteKey : QString();
}

QString LibraryModel::novelSiteName(int row) const
{
    if (row < 0 || row >= m_novels.size())
        return QString();
    const QString key = m_novels[row].siteKey;
    return key.isEmpty() ? QString() : NovelPaths::siteDisplayName(key);
}

QString LibraryModel::novelRootPath(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].rootPath : QString();
}

int LibraryModel::novelChapterCount(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].chapterCount : 0;
}

QStringList LibraryModel::novelChapterNames(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].chapterNames : QStringList();
}

QString LibraryModel::novelChapterName(int row, int chapterIndex) const
{
    const QStringList names = novelChapterNames(row);
    if (chapterIndex < 0 || chapterIndex >= names.size())
        return QString();
    return names[chapterIndex];
}

QString LibraryModel::novelChapterTitle(int row, int chapterIndex) const
{
    if (row < 0 || row >= m_novels.size())
        return QString();
    const QStringList names = novelChapterNames(row);
    if (chapterIndex < 0 || chapterIndex >= names.size())
        return QString();
    const QString base = names.at(chapterIndex);
    const Novel &novel = m_novels.at(row);
    if (novel.readChapterHeaders && !novel.chapterTitles.contains(base)) {
        QFile file(QDir(novel.sourceDir).filePath(base + QStringLiteral(".txt")));
        QString title;
        if (file.open(QIODevice::ReadOnly)) {
            // 保持原有 8KiB 上限，单行超长正文也不会被当作标题全部读入。
            title = TxtExporter::chapterTitleFromDocument(QString::fromUtf8(file.read(8192)));
        }
        novel.chapterTitles.insert(base, title.isEmpty() ? base : title);
    }
    return novel.chapterTitles.value(base, base);
}

bool LibraryModel::novelChapterTranslated(int row, int chapterIndex) const
{
    if (row < 0 || row >= m_novels.size())
        return false;
    const QStringList files = novelChapterNames(row);
    if (chapterIndex < 0 || chapterIndex >= files.size())
        return false;
    const QString name = files[chapterIndex] + QStringLiteral(".txt");
    return QFileInfo::exists(QDir(m_novels[row].translationDir).filePath(name));
}

int LibraryModel::novelTranslatedCount(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].translatedCount : 0;
}

int LibraryModel::novelDownloadedCount(int row) const
{
    return (row >= 0 && row < m_novels.size()) ? m_novels[row].chapterCount : 0;
}
