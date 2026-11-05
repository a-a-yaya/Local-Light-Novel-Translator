#include "ReaderModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include "cjdlexporter.h"
#include "txtexporter.h"

namespace {
int chapterFileIndex(const QStringList &files, const QString &baseName)
{
    for (int i = 0; i < files.size(); ++i) {
        if (QFileInfo(files.at(i)).completeBaseName() == baseName)
            return i;
    }
    return -1;
}
} // namespace

ReaderModel::ReaderModel(QObject *parent)
    : QObject(parent)
{
    connect(this, &ReaderModel::bookmarksChanged, this, &ReaderModel::currentBookmarkedChanged);
}

QString ReaderModel::currentDocumentPath() const
{
    if (m_readingMode == QStringLiteral("translation")) return m_currentTranslationPath;
    if (m_readingMode == QStringLiteral("bilingual")) return m_currentBilingualPath;
    return {}; // 日文原文始终只读。
}

QString ReaderModel::editedPath(const QString &path) const
{
    if (path.isEmpty()) return {};
    return QFileInfo(path).dir().filePath(QStringLiteral(".reader-edits/") + QFileInfo(path).fileName());
}

QString ReaderModel::versionPath(const QString &path) const
{
    const QString edited = editedPath(path);
    return !m_viewingOriginal && QFileInfo::exists(path) && QFileInfo::exists(edited) ? edited : path;
}

bool ReaderModel::hasEdits() const
{
    const QString path = currentDocumentPath();
    return !path.isEmpty() && QFileInfo::exists(path) && QFileInfo::exists(editedPath(path));
}

bool ReaderModel::canEdit() const
{
    const QString path = currentDocumentPath();
    return !m_viewingOriginal && !path.isEmpty() && QFileInfo(path).isFile()
        && (m_readingMode != QStringLiteral("bilingual") || m_bilingualAvailable);
}

bool ReaderModel::editDirty() const
{
    return m_editing && (m_readingMode == QStringLiteral("bilingual")
        ? m_editorDocument != m_initialEditorDocument : m_editorText != m_initialEditorText);
}

QVariantList ReaderModel::editorBlocks() const
{
    return m_editorDocument.value(QStringLiteral("blocks")).toArray().toVariantList();
}

void ReaderModel::setEditorText(const QString &text)
{
    if (!m_editing || m_readingMode != QStringLiteral("translation") || m_editorText == text) return;
    m_editorText = text;
    emit draftChanged();
    emit editStateChanged();
}

void ReaderModel::setBilingualDraft(int index, const QString &translation)
{
    if (!m_editing || m_readingMode != QStringLiteral("bilingual")) return;
    auto blocks = m_editorDocument.value(QStringLiteral("blocks")).toArray();
    if (index < 0 || index >= blocks.size()) return;
    auto block = blocks.at(index).toObject();
    block.insert(QStringLiteral("translation"), translation);
    blocks[index] = block;
    m_editorDocument.insert(QStringLiteral("blocks"), blocks);
    emit draftChanged();
    emit editStateChanged();
}

QString ReaderModel::bilingualDraft(int index) const
{
    const auto blocks = m_editorDocument.value(QStringLiteral("blocks")).toArray();
    return index >= 0 && index < blocks.size()
        ? blocks.at(index).toObject().value(QStringLiteral("translation")).toString() : QString();
}

QVariantList ReaderModel::findEditorMatches(const QString &query, bool caseSensitive) const
{
    QVariantList matches;
    if (!m_editing || query.isEmpty()) return matches;
    auto scan = [&](const QString &text, int block) {
        int start = 0;
        while ((start = text.indexOf(query, start, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)) >= 0) {
            matches.append(QVariantMap{{"block", block}, {"start", start}, {"length", query.size()}});
            start += query.size();
        }
    };
    if (m_readingMode == QStringLiteral("translation")) scan(m_editorText, -1);
    else if (m_readingMode == QStringLiteral("bilingual")) {
        const auto blocks = m_editorDocument.value("blocks").toArray();
        for (int i = 0; i < blocks.size(); ++i) scan(blocks[i].toObject().value("translation").toString(), i);
    }
    return matches;
}

int ReaderModel::replaceEditorMatches(const QString &query, const QString &replacement,
                                      int matchIndex, bool caseSensitive)
{
    const auto matches = findEditorMatches(query, caseSensitive);
    if (matches.isEmpty() || matchIndex >= matches.size() || matchIndex < -1) return 0;
    auto blocks = m_editorDocument.value("blocks").toArray();
    const int first = matchIndex < 0 ? 0 : matchIndex;
    const int last = matchIndex < 0 ? matches.size() - 1 : matchIndex;
    // 从后往前替换，索引不受前面文本长度变化影响；替换内容不再参与本轮查找。
    for (int i = last; i >= first; --i) {
        const auto match = matches[i].toMap();
        const int blockIndex = match.value("block").toInt();
        QString text = blockIndex < 0 ? m_editorText : blocks[blockIndex].toObject().value("translation").toString();
        text.replace(match.value("start").toInt(), match.value("length").toInt(), replacement);
        if (blockIndex < 0) m_editorText = text;
        else {
            auto block = blocks[blockIndex].toObject();
            block.insert("translation", text);
            blocks[blockIndex] = block;
        }
    }
    if (m_readingMode == QStringLiteral("bilingual")) m_editorDocument.insert("blocks", blocks);
    emit editorContentChanged();
    emit draftChanged();
    emit editStateChanged();
    return last - first + 1;
}

bool ReaderModel::beginEditing()
{
    if (m_editing) return true;
    if (!canEdit()) { emit readerError(QStringLiteral("当前内容不可编辑；日文原文和原版均为只读")); return false; }
    const QString path = currentDocumentPath();
    QFile base(path);
    QFile version(versionPath(path));
    if (!base.open(QIODevice::ReadOnly) || !version.open(QIODevice::ReadOnly)) {
        emit readerError(QStringLiteral("无法读取章节，未开始编辑")); return false;
    }
    m_editBaseBytes = base.readAll();
    m_editVersionBytes = version.readAll();
    m_editHadVersion = hasEdits();
    if (m_readingMode == QStringLiteral("bilingual")) {
        const auto document = QJsonDocument::fromJson(m_editVersionBytes);
        const auto root = document.object();
        const auto blocks = root.value(QStringLiteral("blocks"));
        if (!document.isObject() || !blocks.isArray() || blocks.toArray().isEmpty()) {
            emit readerError(QStringLiteral("双语文件格式无效，无法编辑")); return false;
        }
        for (const auto &entry : blocks.toArray()) {
            const auto block = entry.toObject();
            if (!entry.isObject() || !block.value(QStringLiteral("source")).isString()
                || !block.value(QStringLiteral("translation")).isString()) {
                emit readerError(QStringLiteral("双语段落格式无效，无法编辑")); return false;
            }
        }
        m_editorDocument = m_initialEditorDocument = root;
    } else {
        QByteArray bytes = m_editVersionBytes;
        if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
        m_editorText = QString::fromUtf8(bytes);
        m_editorText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        m_initialEditorText = m_editorText;
    }
    m_editing = true;
    emit draftChanged();
    emit editorContentChanged();
    emit editStateChanged();
    return true;
}

bool ReaderModel::saveEdits()
{
    if (!m_editing) return false;
    if (!editDirty()) { cancelEditing(); emit editsSaved(); return true; }
    const QString path = currentDocumentPath();
    const QString edited = editedPath(path);
    QFile base(path);
    QFile version(versionPath(path));
    if (!base.open(QIODevice::ReadOnly) || !version.open(QIODevice::ReadOnly)
        || base.readAll() != m_editBaseBytes || version.readAll() != m_editVersionBytes
        || hasEdits() != m_editHadVersion) {
        emit readerError(QStringLiteral("章节文件已被其它操作改动，未覆盖文件；编辑草稿仍保留，请先复制备份")); return false;
    }
    base.close();
    version.close();
    QByteArray bytes;
    if (m_readingMode == QStringLiteral("bilingual")) {
        bytes = QJsonDocument(m_editorDocument).toJson(QJsonDocument::Indented);
    } else {
        if (m_editorText.trimmed().isEmpty()) { emit readerError(QStringLiteral("正文不能为空，未保存")); return false; }
        bytes = m_editorText.toUtf8();
    }
    // 基础章节永不写入；只维护一份最新编辑稿，不生成历史编辑稿或重复备份。
    QSaveFile file(edited);
    if (!QDir().mkpath(QFileInfo(edited).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        emit readerError(QStringLiteral("保存失败，原版和上次编辑稿均保留：%1").arg(file.errorString())); return false;
    }
    cancelEditing();
    m_viewingOriginal = false;
    loadChapterContent(m_currentIndex, false);
    emit editsSaved();
    return true;
}

void ReaderModel::cancelEditing()
{
    m_editing = false;
    m_editorText.clear();
    m_initialEditorText.clear();
    m_editorDocument = m_initialEditorDocument = {};
    m_editBaseBytes.clear();
    m_editVersionBytes.clear();
    emit draftChanged();
    emit editStateChanged();
}

bool ReaderModel::setViewingOriginal(bool original)
{
    if (m_editing) { emit readerError(QStringLiteral("请先保存或取消本章编辑")); return false; }
    m_viewingOriginal = original && hasEdits();
    loadChapterContent(m_currentIndex, false);
    return true;
}

void ReaderModel::openNovel(const QString &sourceDir,
                            const QString &translationDir,
                            const QString &title,
                            const QString &novelKey)
{
    if (m_editing) { emit readerError(QStringLiteral("请先保存或取消本章编辑")); return; }
    prepareOpen();
    m_sourceDir = sourceDir;
    m_translationDir = translationDir;
    m_novelTitle = title;
    m_novelKey = novelKey;
    refreshChapterFiles();

    // 恢复上次语言与进度；默认有译文看中文，否则看日文
    loadNovelProgress();
    if (m_language != QStringLiteral("jp") && m_language != QStringLiteral("zh"))
        m_language = m_translationFiles.isEmpty() ? QStringLiteral("jp") : QStringLiteral("zh");
    if (m_language == QStringLiteral("zh") && !m_chapterFiles.isEmpty()
        && translationIndexForSourceIndex(m_zhIndex) < 0) {
        int firstTranslatedSource = -1;
        for (int i = 0; i < m_chapterFiles.size(); ++i) {
            if (translationIndexForSourceIndex(i) >= 0) {
                firstTranslatedSource = i;
                break;
            }
        }
        if (firstTranslatedSource >= 0)
            m_zhIndex = firstTranslatedSource;
        else
            m_language = QStringLiteral("jp");
    }
    m_readingMode = (m_language == QStringLiteral("jp"))
                        ? QStringLiteral("original")
                        : QStringLiteral("translation");
    if (m_resumeMode == QStringLiteral("bilingual")) m_readingMode = m_resumeMode;
    emit readingModeChanged();
    m_currentIndex = (m_language == QStringLiteral("jp")) ? m_jpIndex : m_zhIndex;
    if (m_readingMode == QStringLiteral("bilingual")) m_currentIndex = m_bilingualIndex;
    if (m_currentIndex < 0 || m_currentIndex >= m_totalChapters)
        m_currentIndex = 0;
    loadChapter(m_currentIndex);
}

void ReaderModel::openNovelAt(const QString &sourceDir,
                              const QString &translationDir,
                              const QString &title,
                              const QString &novelKey,
                              const QString &language,
                              int chapterIndex)
{
    if (m_editing) { emit readerError(QStringLiteral("请先保存或取消本章编辑")); return; }
    prepareOpen();
    m_sourceDir = sourceDir;
    m_translationDir = translationDir;
    m_novelTitle = title;
    m_novelKey = novelKey;
    refreshChapterFiles();
    loadNovelProgress();

    // 用户明确点击了“日文/中文”中的某一章，语言与章节必须按本次点击确定，
    // 不能被上次阅读时保存的 lastLanguage 覆盖。
    m_language = (language == QStringLiteral("jp")) ? QStringLiteral("jp") : QStringLiteral("zh");
    m_readingMode = (m_language == QStringLiteral("jp"))
                        ? QStringLiteral("original")
                        : QStringLiteral("translation");
    emit readingModeChanged();
    if (chapterIndex < 0 || chapterIndex >= m_totalChapters)
        chapterIndex = 0;
    if (m_language == QStringLiteral("zh") && !m_chapterFiles.isEmpty()
        && translationIndexForSourceIndex(chapterIndex) < 0) {
        // A Chinese catalog item should always have an exact matching file.
        // If it became stale between display and click, keep the selected source
        // chapter and open it in Japanese rather than showing a neighboring file.
        m_language = QStringLiteral("jp");
        m_readingMode = QStringLiteral("original");
        emit languageChanged();
        emit readingModeChanged();
    }
    loadChapter(chapterIndex);
}

bool ReaderModel::openNovelAtFile(const QString &sourceDir,
                                  const QString &translationDir,
                                  const QString &title,
                                  const QString &novelKey,
                                  const QString &language,
                                  const QString &chapterBaseName)
{
    if (m_editing) { emit readerError(QStringLiteral("请先保存或取消本章编辑")); return false; }
    if (chapterBaseName.trimmed().isEmpty())
        return false;

    prepareOpen();

    m_sourceDir = sourceDir;
    m_translationDir = translationDir;
    refreshChapterFiles();

    int sourceIndex = chapterFileIndex(m_chapterFiles, chapterBaseName);

    if (sourceIndex < 0 && language == QStringLiteral("zh") && m_chapterFiles.isEmpty()) {
        sourceIndex = chapterFileIndex(m_translationFiles, chapterBaseName);
    }

    // Both catalog tabs carry the displayed chapter's base filename. Resolve
    // it against the source list (or translation-only list), then let the
    // reader map that exact basename to its translation. Never reinterpret a
    // stale row number as another file.
    if (sourceIndex < 0) {
        qWarning() << "[Reader] chapter file not found in source catalog:" << chapterBaseName;
        return false;
    }

    openNovelAt(sourceDir, translationDir, title, novelKey, language, sourceIndex);
    return true;
}

void ReaderModel::openNovelBilingualFile(const QString &sourceDir,
                                         const QString &translationDir,
                                         const QString &title,
                                         const QString &novelKey,
                                         const QString &chapterBaseName)
{
    if (m_editing) { emit readerError(QStringLiteral("请先保存或取消本章编辑")); return; }
    if (chapterBaseName.trimmed().isEmpty())
        return;

    prepareOpen();

    m_sourceDir = sourceDir;
    m_translationDir = translationDir;
    m_novelTitle = title;
    m_novelKey = novelKey;
    refreshChapterFiles();
    loadNovelProgress();

    int sourceIndex = chapterFileIndex(m_chapterFiles, chapterBaseName);

    if (sourceIndex < 0 && m_chapterFiles.isEmpty()) {
        sourceIndex = chapterFileIndex(m_translationFiles, chapterBaseName);
    }

    if (sourceIndex < 0) {
        qWarning() << "[Reader] bilingual chapter file not found:" << chapterBaseName;
        return;
    }

    // 双语语言标识仍为 zh，阅读记录按 bilingual 目录单独保存。
    m_language = QStringLiteral("zh");
    m_readingMode = QStringLiteral("bilingual");
    emit languageChanged();
    emit readingModeChanged();
    loadChapter(sourceIndex);
}

QString ReaderModel::catalogTitle(int chapterNumber)
{
    const QFileInfo info(QDir(m_sourceDir).filePath(QStringLiteral("../novel.json")));
    const QString path = info.absoluteFilePath();
    if (path == m_catalogPath && info.size() == m_catalogSize
        && info.lastModified() == m_catalogModified)
        return m_catalogTitles.value(chapterNumber);

    m_catalogPath.clear();
    m_catalogTitles.clear();
    QFile metadata(path);
    if (!metadata.open(QIODevice::ReadOnly))
        return {};
    const auto document = QJsonDocument::fromJson(metadata.readAll());
    if (!document.isObject())
        return {};
    const auto chapters = document.object().value(QStringLiteral("chapters")).toArray();
    for (const auto &entry : chapters) {
        const auto chapter = entry.toObject();
        const int number = chapter.value(QStringLiteral("number")).toInt();
        const QString title = chapter.value(QStringLiteral("title")).toString();
        // 与原逐条查找一致：同一章节编号采用第一个非空标题。
        if (!title.isEmpty() && !m_catalogTitles.contains(number))
            m_catalogTitles.insert(number, title);
    }
    m_catalogPath = path;
    m_catalogSize = info.size();
    m_catalogModified = info.lastModified();
    return m_catalogTitles.value(chapterNumber);
}

void ReaderModel::refreshChapterFiles()
{
    m_chapterFiles.clear();
    m_translationFiles.clear();

    QDir sd(m_sourceDir);
    if (sd.exists())
        m_chapterFiles = sd.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files, QDir::Name);

    QDir td(m_translationDir);
    if (td.exists())
        m_translationFiles = td.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files, QDir::Name);

    // 章节总数以原文目录为准，缺少原文时退回到译文目录
    m_totalChapters = m_chapterFiles.isEmpty() ? m_translationFiles.size() : m_chapterFiles.size();
    emit totalChaptersChanged();
    emit novelTitleChanged();
}

int ReaderModel::translationIndexForSourceIndex(int sourceIndex) const
{
    if (m_chapterFiles.isEmpty())
        return (sourceIndex >= 0 && sourceIndex < m_translationFiles.size()) ? sourceIndex : -1;
    if (sourceIndex < 0 || sourceIndex >= m_chapterFiles.size())
        return -1;
    const QString sourceBase = QFileInfo(m_chapterFiles[sourceIndex]).completeBaseName();
    return chapterFileIndex(m_translationFiles, sourceBase);
}

void ReaderModel::loadChapter(int index)
{
    if (m_editing) { emit readerError(QStringLiteral("请先保存或取消本章编辑")); return; }
    saveNovelProgress();
    loadChapterContent(index, true);
}

void ReaderModel::loadChapterContent(int index, bool resetVersion)
{
    if (resetVersion) m_viewingOriginal = false;
    if (m_totalChapters == 0) {
        m_chapterLoaded = false;
        m_positionParagraph = 0;
        m_positionOffset = 0;
        // 空小说不能继承上一本文本或编辑目标，否则可能把修改保存到上一部作品。
        m_currentIndex = 0;
        m_currentTranslationPath.clear();
        m_currentBilingualPath.clear();
        m_paragraphs.clear();
        m_originalParagraphs.clear();
        m_bilingualSources.clear();
        m_bilingualTranslations.clear();
        m_bilingualAvailable = false;
        m_chapterTitle.clear();
        emit paragraphsChanged();
        emit originalParagraphsChanged();
        emit bilingualSourcesChanged();
        emit bilingualTranslationsChanged();
        emit bilingualAvailableChanged();
        emit chapterTitleChanged();
        emit currentIndexChanged();
        emit editStateChanged();
        emit positionChanged();
        emit bookmarksChanged();
        emit restorePositionRequested();
        return;
    }
    index = qBound(0, index, m_totalChapters - 1);
    m_currentIndex = index;

    QString sourceFile = (index < m_chapterFiles.size()) ? m_chapterFiles[index] : QString();
    QString transFile;
    if (m_chapterFiles.isEmpty()) {
        if (index < m_translationFiles.size())
            transFile = m_translationFiles[index];
    } else {
        const int transIndex = translationIndexForSourceIndex(index);
        if (transIndex >= 0)
            transFile = m_translationFiles[transIndex];
    }

    m_currentTranslationPath = transFile.isEmpty() ? QString() : QDir(m_translationDir).filePath(transFile);
    m_currentBilingualPath.clear();
    m_originalParagraphs = sourceFile.isEmpty()
        ? QStringList()
        : readParagraphs(QDir(m_sourceDir).filePath(sourceFile), true);
    m_paragraphs = transFile.isEmpty()
        ? QStringList()
        : readParagraphs(QDir(m_translationDir).filePath(transFile), false);

    // 没有译文时退回原文，保证阅读器始终有内容
    if (m_language != QStringLiteral("zh") && m_paragraphs.isEmpty() && !m_originalParagraphs.isEmpty())
        m_paragraphs = m_originalParagraphs;

    m_chapterTitle = QFileInfo((m_language == QStringLiteral("zh") && !transFile.isEmpty())
                                   ? transFile : sourceFile).completeBaseName();
    const QString indexedTitle = catalogTitle(m_chapterTitle.toInt());
    const bool foundCatalogTitle = !indexedTitle.isEmpty();
    if (foundCatalogTitle)
        m_chapterTitle = indexedTitle;
    if (!foundCatalogTitle && !sourceFile.isEmpty()) {
        QFile titleFile(QDir(m_sourceDir).filePath(sourceFile));
        if (titleFile.open(QIODevice::ReadOnly)) {
            // 文件头只有标题和 URL 两行，不必为取标题再读一次整章正文。
            QByteArray header = titleFile.readLine();
            header += titleFile.readLine();
            const QString headerTitle =
                TxtExporter::chapterTitleFromDocument(QString::fromUtf8(header));
            if (!headerTitle.isEmpty() && headerTitle != m_chapterTitle)
                m_chapterTitle = headerTitle;
        }
    }

    // 中日对照模式：优先读取 CJDL 资源，不临时拼接 DOWNLOAD+CHDL。
    m_bilingualSources.clear();
    m_bilingualTranslations.clear();
    m_bilingualAvailable = false;
    QString baseName;
    if (!sourceFile.isEmpty())
        baseName = QFileInfo(sourceFile).completeBaseName();
    else if (!transFile.isEmpty())
        baseName = QFileInfo(transFile).completeBaseName();
    const QString cjdlKey = m_cjdlKey.isEmpty() ? m_novelKey : m_cjdlKey;
    if (!m_cjdlDir.isEmpty() && !cjdlKey.isEmpty() && !baseName.isEmpty()) {
        const QString cjdlPath = CjdlExporter::cjdlPathFor(m_cjdlDir, cjdlKey, baseName);
        m_currentBilingualPath = cjdlPath;
        QList<CjdlBlock> blocks;
        QString workId;
        QString chapterId;
        QString title;
        if (CjdlExporter::read(versionPath(cjdlPath), blocks, workId, chapterId, title)) {
            for (const CjdlBlock &b : blocks) {
                m_bilingualSources << b.source;
                m_bilingualTranslations << b.translation;
            }
            m_bilingualAvailable = !m_bilingualSources.isEmpty();
        }
    }

    m_language = m_readingMode == QStringLiteral("original") ? QStringLiteral("jp") : QStringLiteral("zh");
    // 三个目录各自维护最后停留位置与已读章节。
    if (m_language == QStringLiteral("jp")) {
        m_jpIndex = index;
        m_readJp.insert(index);
    } else if (m_readingMode == QStringLiteral("bilingual")) {
        m_bilingualIndex = index;
        m_readBilingual.insert(index);
    } else {
        m_zhIndex = index;
        m_readZh.insert(index);
    }

    m_chapterLoaded = true;
    restoreChapterPosition();

    emit editStateChanged();
    emit paragraphsChanged();
    emit originalParagraphsChanged();
    emit bilingualSourcesChanged();
    emit bilingualTranslationsChanged();
    emit bilingualAvailableChanged();
    emit chapterTitleChanged();
    emit currentIndexChanged();
    emit languageChanged();
    saveCurrentChapter();
    emit bookmarksChanged();
    emit restorePositionRequested();
}

void ReaderModel::nextChapter()
{
    int next = m_currentIndex + 1;
    while (next < m_totalChapters) {
        if (m_language != QStringLiteral("zh") || translationIndexForSourceIndex(next) >= 0) {
            loadChapter(next);
            return;
        }
        ++next;
    }
}

void ReaderModel::previousChapter()
{
    int previous = m_currentIndex - 1;
    while (previous >= 0) {
        if (m_language != QStringLiteral("zh") || translationIndexForSourceIndex(previous) >= 0) {
            loadChapter(previous);
            return;
        }
        --previous;
    }
}

QStringList ReaderModel::readParagraphs(const QString &path, bool isSource) const
{
    QStringList result;
    QFile f(isSource ? path : versionPath(path));
    if (!f.open(QIODevice::ReadOnly))
        return result;
    QByteArray raw = f.readAll();
    f.close();

    // 去掉 UTF-8 BOM
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3);
    QString text = QString::fromUtf8(raw);

    // 只按完整下载头结构区分标题/网址与正文，不再盲目删除前几行。
    QString header;
    TxtExporter::splitChapterDocument(text, &header, &text);

    QStringList parts = text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")));
    for (const QString &part : parts) {
        QString p = part.trimmed();
        if (!p.isEmpty())
            result << p;
    }
    return result;
}

void ReaderModel::setReadingMode(const QString &mode)
{
    if (m_readingMode == mode) return;
    if (m_editing) { emit readerError(QStringLiteral("请先保存或取消本章编辑")); return; }
    if (mode != QStringLiteral("original") && mode != QStringLiteral("translation")
        && mode != QStringLiteral("bilingual")) return;
    saveNovelProgress();
    m_readingMode = mode;
    m_viewingOriginal = false;
    emit readingModeChanged();
    loadChapterContent(m_currentIndex, false);
}

void ReaderModel::setFontSize(int size)
{
    size = qBound(12, size, 40);
    if (m_fontSize == size)
        return;
    m_fontSize = size;
    emit fontSizeChanged();
}

void ReaderModel::setLineSpacing(qreal spacing)
{
    spacing = qBound(1.0, spacing, 3.0);
    if (qFuzzyCompare(m_lineSpacing, spacing))
        return;
    m_lineSpacing = spacing;
    emit lineSpacingChanged();
}

void ReaderModel::setFontFamily(const QString &family)
{
    if (m_fontFamily == family || family.trimmed().isEmpty())
        return;
    m_fontFamily = family;
    emit fontFamilyChanged();
}

void ReaderModel::setMaxWidth(qreal width)
{
    width = qBound(400.0, width, 1400.0);
    if (qFuzzyCompare(m_maxWidth, width))
        return;
    m_maxWidth = width;
    emit maxWidthChanged();
}

void ReaderModel::setReaderTheme(const QString &theme)
{
    if (m_readerTheme == theme)
        return;
    m_readerTheme = theme;
    emit readerThemeChanged();
}

void ReaderModel::setCjdlDir(const QString &dir)
{
    if (m_cjdlDir == dir)
        return;
    m_cjdlDir = dir;
}

void ReaderModel::setCjdlKey(const QString &key)
{
    m_cjdlKey = key;
}

QString ReaderModel::progressGroup(const QString &novelKey) const
{
    return QStringLiteral("reading/") + novelKey;
}

void ReaderModel::prepareOpen()
{
    m_catalogPath.clear();
    m_catalogTitles.clear();
    saveNovelProgress();
    m_chapterLoaded = false;
}

QString ReaderModel::currentChapterBase() const
{
    const auto &files = m_chapterFiles.isEmpty() ? m_translationFiles : m_chapterFiles;
    return m_currentIndex >= 0 && m_currentIndex < files.size()
        ? QFileInfo(files[m_currentIndex]).completeBaseName() : QString();
}

int ReaderModel::chapterIndexForBase(const QString &base) const
{
    const auto &files = m_chapterFiles.isEmpty() ? m_translationFiles : m_chapterFiles;
    for (int i = 0; i < files.size(); ++i)
        if (QFileInfo(files[i]).completeBaseName() == base) return i;
    return -1;
}

int ReaderModel::paragraphCount() const
{
    return m_readingMode == QStringLiteral("original") ? m_originalParagraphs.size()
        : m_readingMode == QStringLiteral("bilingual") ? m_bilingualSources.size() : m_paragraphs.size();
}

void ReaderModel::setPosition(int paragraph, qreal offset)
{
    if (!m_chapterLoaded || m_editing) return;
    paragraph = qBound(0, paragraph, qMax(0, paragraphCount() - 1));
    offset = std::isfinite(offset) ? qBound(qreal(0), offset, qreal(1)) : 0;
    if (paragraph == m_positionParagraph && qFuzzyCompare(offset + 1, m_positionOffset + 1)) return;
    const bool changedParagraph = paragraph != m_positionParagraph;
    m_positionParagraph = paragraph;
    m_positionOffset = offset;
    emit positionChanged();
    if (changedParagraph) emit currentBookmarkedChanged();
}

void ReaderModel::restoreChapterPosition()
{
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    settings.beginGroup(progressGroup(m_novelKey) + QStringLiteral("/positions/") + m_readingMode);
    const auto position = settings.value(currentChapterBase()).toMap();
    m_positionParagraph = qBound(0, position.value("paragraph").toInt(), qMax(0, paragraphCount() - 1));
    const qreal offset = position.value("offset").toDouble();
    m_positionOffset = std::isfinite(offset) ? qBound(qreal(0), offset, qreal(1)) : 0;
    emit positionChanged();
}

QVariantList ReaderModel::bookmarks() const
{
    if (m_novelKey.isEmpty()) return {};
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    const QByteArray bytes = settings.value(progressGroup(m_novelKey) + QStringLiteral("/bookmarks/")
                                           + m_readingMode).toByteArray();
    QVariantList list = QJsonDocument::fromJson(bytes).array().toVariantList();
    const auto &files = m_chapterFiles.isEmpty() ? m_translationFiles : m_chapterFiles;
    QHash<QString, int> chapterIndices;
    chapterIndices.reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
        const QString base = QFileInfo(files[i]).completeBaseName();
        if (!chapterIndices.contains(base)) chapterIndices.insert(base, i);
    }
    for (auto &entry : list) {
        auto mark = entry.toMap();
        mark.insert("chapterIndex", chapterIndices.value(mark.value("chapter").toString(), -1));
        entry = mark;
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const auto x = a.toMap(), y = b.toMap();
        return x.value("chapterIndex").toInt() == y.value("chapterIndex").toInt()
            ? x.value("paragraph").toInt() < y.value("paragraph").toInt()
            : x.value("chapterIndex").toInt() < y.value("chapterIndex").toInt();
    });
    return list;
}

bool ReaderModel::currentBookmarked() const
{
    if (m_novelKey.isEmpty()) return false;
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    const QByteArray bytes = settings.value(progressGroup(m_novelKey) + QStringLiteral("/bookmarks/")
                                           + m_readingMode).toByteArray();
    const QString chapter = currentChapterBase();
    for (const auto &entry : QJsonDocument::fromJson(bytes).array()) {
        const auto mark = entry.toObject();
        if (mark.value("chapter").toVariant().toString() == chapter
            && mark.value("paragraph").toVariant().toInt() == m_positionParagraph) return true;
    }
    return false;
}

void ReaderModel::removeBookmark(const QString &chapter, int paragraph)
{
    if (m_novelKey.isEmpty()) return;
    auto list = bookmarks();
    for (int i = list.size() - 1; i >= 0; --i) {
        const auto mark = list[i].toMap();
        if (mark.value("chapter").toString() == chapter && mark.value("paragraph").toInt() == paragraph)
            list.removeAt(i);
    }
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    settings.setValue(progressGroup(m_novelKey) + QStringLiteral("/bookmarks/") + m_readingMode,
                      QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
    emit bookmarksChanged();
}

void ReaderModel::toggleBookmark()
{
    if (!m_chapterLoaded || m_editing || paragraphCount() == 0) return;
    if (currentBookmarked()) { removeBookmark(currentChapterBase(), m_positionParagraph); return; }
    auto list = bookmarks();
    const auto &paragraphs = m_readingMode == QStringLiteral("original") ? m_originalParagraphs
        : m_readingMode == QStringLiteral("bilingual") ? m_bilingualTranslations : m_paragraphs;
    list.append(QVariantMap{{"chapter", currentChapterBase()}, {"title", m_chapterTitle},
        {"paragraph", m_positionParagraph}, {"offset", m_positionOffset},
        {"preview", paragraphs.value(m_positionParagraph).left(100)}});
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    settings.setValue(progressGroup(m_novelKey) + QStringLiteral("/bookmarks/") + m_readingMode,
                      QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
    emit bookmarksChanged();
}

void ReaderModel::openBookmark(int index)
{
    if (m_editing) return;
    const auto list = bookmarks();
    if (index < 0 || index >= list.size()) return;
    const auto mark = list[index].toMap();
    const int chapter = chapterIndexForBase(mark.value("chapter").toString());
    if (chapter < 0 || (m_readingMode == QStringLiteral("translation") && translationIndexForSourceIndex(chapter) < 0)) {
        emit readerError(QStringLiteral("书签对应章节已不存在")); return;
    }
    loadChapter(chapter);
    setPosition(mark.value("paragraph").toInt(), mark.value("offset").toDouble());
    saveNovelProgress();
    emit restorePositionRequested();
}

void ReaderModel::saveCurrentChapter()
{
    saveNovelProgress();
}

void ReaderModel::saveProgress()
{
    saveNovelProgress();
}

void ReaderModel::loadProgress()
{
    loadNovelProgress();
}

void ReaderModel::saveNovelProgress()
{
    if (m_novelKey.isEmpty() || !m_chapterLoaded)
        return;

    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    settings.beginGroup(progressGroup(m_novelKey));
    const QString prefix = m_readingMode == QStringLiteral("original") ? QStringLiteral("jp")
        : m_readingMode == QStringLiteral("bilingual") ? QStringLiteral("bi") : QStringLiteral("zh");
    settings.setValue(prefix + QStringLiteral("Index"), m_currentIndex);
    settings.setValue(prefix + QStringLiteral("Chapter"), currentChapterBase());
    settings.setValue(QStringLiteral("lastLanguage"), m_language);
    settings.setValue(QStringLiteral("lastMode"), m_readingMode);
    const auto &read = prefix == "jp" ? m_readJp : prefix == "bi" ? m_readBilingual : m_readZh;
    QStringList readList;
    for (int i : read) readList << QString::number(i);
    std::sort(readList.begin(), readList.end());
    settings.setValue(prefix + QStringLiteral("Read"), readList);
    // 最后停留位置直接覆盖，不以读过的最大章节/段落代替。
    settings.setValue(QStringLiteral("positions/") + m_readingMode + QLatin1Char('/') + currentChapterBase(),
                      QVariantMap{{"paragraph", m_positionParagraph}, {"offset", m_positionOffset}});
    settings.endGroup();
}

void ReaderModel::loadNovelProgress()
{
    m_jpIndex = 0;
    m_zhIndex = 0;
    m_bilingualIndex = 0;
    m_resumeMode.clear();
    m_readJp.clear();
    m_readZh.clear();
    m_readBilingual.clear();

    if (m_novelKey.isEmpty())
        return;

    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    settings.beginGroup(progressGroup(m_novelKey));
    m_jpIndex = settings.value(QStringLiteral("jpIndex"), 0).toInt();
    m_zhIndex = settings.value(QStringLiteral("zhIndex"), 0).toInt();
    m_bilingualIndex = settings.value(QStringLiteral("biIndex"), 0).toInt();
    // 优先文件标识，旧记录只有索引时兼容读取。
    auto resumeIndex = [&](const QString &prefix, int previous) {
        const int resolved = chapterIndexForBase(settings.value(prefix + QStringLiteral("Chapter")).toString());
        return resolved >= 0 ? resolved : previous;
    };
    m_jpIndex = resumeIndex("jp", m_jpIndex);
    m_zhIndex = resumeIndex("zh", m_zhIndex);
    m_bilingualIndex = resumeIndex("bi", m_bilingualIndex);
    m_language = settings.value(QStringLiteral("lastLanguage"), QStringLiteral("zh")).toString();
    m_resumeMode = settings.value(QStringLiteral("lastMode")).toString();

    const QStringList jpRead = settings.value(QStringLiteral("jpRead")).toStringList();
    for (const QString &s : jpRead)
        m_readJp.insert(s.toInt());
    const QStringList zhRead = settings.value(QStringLiteral("zhRead")).toStringList();
    for (const QString &s : zhRead)
        m_readZh.insert(s.toInt());
    for (const QString &s : settings.value(QStringLiteral("biRead")).toStringList())
        m_readBilingual.insert(s.toInt());
    settings.endGroup();
}

bool ReaderModel::isChapterRead(const QString &novelKey, const QString &language, int index) const
{
    if (novelKey.isEmpty())
        return false;
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    settings.beginGroup(progressGroup(novelKey));
    const QString key = language == QStringLiteral("jp") ? QStringLiteral("jpRead")
        : language == QStringLiteral("bilingual") ? QStringLiteral("biRead") : QStringLiteral("zhRead");
    const QStringList read = settings.value(key).toStringList();
    settings.endGroup();
    return read.contains(QString::number(index));
}

void ReaderModel::clearNovelProgress(const QString &novelKey, const QString &mode)
{
    if (novelKey.isEmpty())
        return;
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    if (!mode.isEmpty() && mode != "original" && mode != "translation" && mode != "bilingual") return;
    settings.beginGroup(progressGroup(novelKey));
    if (mode.isEmpty()) settings.remove(QString());
    else {
        const QString prefix = mode == "original" ? QStringLiteral("jp") : mode == "bilingual" ? QStringLiteral("bi") : QStringLiteral("zh");
        settings.remove(prefix + QStringLiteral("Index"));
        settings.remove(prefix + QStringLiteral("Chapter"));
        settings.remove(prefix + QStringLiteral("Read"));
        settings.remove(QStringLiteral("positions/") + mode);
        settings.remove(QStringLiteral("bookmarks/") + mode);
        const QString last = settings.value("lastMode").toString();
        if (last == mode || (last.isEmpty() && mode != "bilingual"
            && settings.value("lastLanguage").toString() == (mode == "original" ? "jp" : "zh"))) {
            settings.remove("lastMode");
            settings.remove("lastLanguage");
        }
    }
    settings.endGroup();

    if (novelKey == m_novelKey) {
        if (mode.isEmpty() || mode == "original") { m_jpIndex = 0; m_readJp.clear(); }
        if (mode.isEmpty() || mode == "translation") { m_zhIndex = 0; m_readZh.clear(); }
        if (mode.isEmpty() || mode == "bilingual") { m_bilingualIndex = 0; m_readBilingual.clear(); }
        if (mode.isEmpty() || mode == m_readingMode) {
            m_chapterLoaded = false; // 避免已退出阅读器的旧模型在下次打开时写回被删除的记录。
            m_positionParagraph = 0;
            m_positionOffset = 0;
            emit positionChanged();
        }
        emit bookmarksChanged();
    }
}
