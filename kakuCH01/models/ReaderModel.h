#ifndef READERMODEL_H
#define READERMODEL_H

#include <QObject>
#include <QStringList>
#include <QSet>
#include <QVariantList>
#include <QJsonObject>
#include <QHash>
#include <QDateTime>

// 阅读器模型：负责读取章节 txt、拆分段落、切换章节与保存阅读进度。
// QML 只展示内容，不直接读写文件。
class ReaderModel : public QObject
{
    Q_OBJECT

    // 译文段落（翻译 / 双语模式使用）
    Q_PROPERTY(QStringList paragraphs READ paragraphs NOTIFY paragraphsChanged)
    // 原文段落（原文 / 双语模式使用）
    Q_PROPERTY(QStringList originalParagraphs READ originalParagraphs NOTIFY originalParagraphsChanged)
    Q_PROPERTY(QStringList bilingualSources READ bilingualSources NOTIFY bilingualSourcesChanged)
    Q_PROPERTY(QStringList bilingualTranslations READ bilingualTranslations NOTIFY bilingualTranslationsChanged)
    Q_PROPERTY(bool bilingualAvailable READ bilingualAvailable NOTIFY bilingualAvailableChanged)
    Q_PROPERTY(QString chapterTitle READ chapterTitle NOTIFY chapterTitleChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int totalChapters READ totalChapters NOTIFY totalChaptersChanged)
    Q_PROPERTY(QString novelTitle READ novelTitle NOTIFY novelTitleChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editStateChanged)
    Q_PROPERTY(bool editDirty READ editDirty NOTIFY editStateChanged)
    Q_PROPERTY(bool hasEdits READ hasEdits NOTIFY editStateChanged)
    Q_PROPERTY(bool viewingOriginal READ viewingOriginal NOTIFY editStateChanged)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY editStateChanged)
    Q_PROPERTY(QString editorText READ editorText WRITE setEditorText NOTIFY editorContentChanged)
    Q_PROPERTY(QVariantList editorBlocks READ editorBlocks NOTIFY editorContentChanged)
    Q_PROPERTY(int positionParagraph READ positionParagraph NOTIFY positionChanged)
    Q_PROPERTY(qreal positionOffset READ positionOffset NOTIFY positionChanged)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY bookmarksChanged)
    Q_PROPERTY(bool currentBookmarked READ currentBookmarked NOTIFY currentBookmarkedChanged)

    // 阅读设置
    Q_PROPERTY(QString readingMode READ readingMode NOTIFY readingModeChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(qreal lineSpacing READ lineSpacing NOTIFY lineSpacingChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(qreal maxWidth READ maxWidth NOTIFY maxWidthChanged)
    Q_PROPERTY(QString readerTheme READ readerTheme NOTIFY readerThemeChanged)

public:
    explicit ReaderModel(QObject *parent = nullptr);

    QStringList paragraphs() const { return m_paragraphs; }
    QStringList originalParagraphs() const { return m_originalParagraphs; }
    QStringList bilingualSources() const { return m_bilingualSources; }
    QStringList bilingualTranslations() const { return m_bilingualTranslations; }
    bool bilingualAvailable() const { return m_bilingualAvailable; }
    QString chapterTitle() const { return m_chapterTitle; }
    int currentIndex() const { return m_currentIndex; }
    int totalChapters() const { return m_totalChapters; }
    QString novelTitle() const { return m_novelTitle; }
    QString language() const { return m_language; }
    bool editing() const { return m_editing; }
    bool editDirty() const;
    bool hasEdits() const;
    bool viewingOriginal() const { return m_viewingOriginal; }
    bool canEdit() const;
    QString editorText() const { return m_editorText; }
    QVariantList editorBlocks() const;
    void setEditorText(const QString &text);
    Q_INVOKABLE bool beginEditing();
    Q_INVOKABLE bool saveEdits();
    Q_INVOKABLE void cancelEditing();
    Q_INVOKABLE bool setViewingOriginal(bool original);
    Q_INVOKABLE void setBilingualDraft(int index, const QString &translation);
    Q_INVOKABLE QString bilingualDraft(int index) const;
    Q_INVOKABLE QVariantList findEditorMatches(const QString &query, bool caseSensitive = false) const;
    Q_INVOKABLE int replaceEditorMatches(const QString &query, const QString &replacement,
                                         int matchIndex = -1, bool caseSensitive = false);
    int positionParagraph() const { return m_positionParagraph; }
    qreal positionOffset() const { return m_positionOffset; }
    Q_INVOKABLE void setPosition(int paragraph, qreal offset = 0);
    QVariantList bookmarks() const;
    bool currentBookmarked() const;
    Q_INVOKABLE void toggleBookmark();
    Q_INVOKABLE void removeBookmark(const QString &chapter, int paragraph);
    Q_INVOKABLE void openBookmark(int index);

    QString readingMode() const { return m_readingMode; }
    int fontSize() const { return m_fontSize; }
    qreal lineSpacing() const { return m_lineSpacing; }
    QString fontFamily() const { return m_fontFamily; }
    qreal maxWidth() const { return m_maxWidth; }
    QString readerTheme() const { return m_readerTheme; }

    Q_INVOKABLE void openNovel(const QString &sourceDir,
                               const QString &translationDir,
                               const QString &title,
                               const QString &novelKey);
    Q_INVOKABLE void openNovelAt(const QString &sourceDir,
                                 const QString &translationDir,
                                 const QString &title,
                                 const QString &novelKey,
                                 const QString &language,
                                 int chapterIndex);
    bool openNovelAtFile(const QString &sourceDir,
                         const QString &translationDir,
                         const QString &title,
                         const QString &novelKey,
                         const QString &language,
                         const QString &chapterBaseName);
    Q_INVOKABLE void openNovelBilingualFile(const QString &sourceDir,
                                            const QString &translationDir,
                                            const QString &title,
                                            const QString &novelKey,
                                            const QString &chapterBaseName);
    Q_INVOKABLE void loadChapter(int index);
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void previousChapter();

    Q_INVOKABLE void setReadingMode(const QString &mode);
    Q_INVOKABLE void setFontSize(int size);
    Q_INVOKABLE void setLineSpacing(qreal spacing);
    Q_INVOKABLE void setFontFamily(const QString &family);
    Q_INVOKABLE void setMaxWidth(qreal width);
    Q_INVOKABLE void setReaderTheme(const QString &theme);
    Q_INVOKABLE void setCjdlDir(const QString &dir);
    // 双语资源在 CJDL 下的相对标识（<siteKey>/<workId>）；为空时退回 novelKey
    Q_INVOKABLE void setCjdlKey(const QString &key);

    // 阅读进度持久化
    Q_INVOKABLE void saveProgress();
    Q_INVOKABLE void loadProgress();

    // 阅读记录查询 / 清除（供书库与详情页使用）
    Q_INVOKABLE bool isChapterRead(const QString &novelKey, const QString &language, int index) const;
    Q_INVOKABLE void clearNovelProgress(const QString &novelKey, const QString &mode = QString());

signals:
    void editStateChanged();
    void editorContentChanged();
    void readerError(const QString &message);
    void editsSaved();
    void draftChanged();
    void positionChanged();
    void restorePositionRequested();
    void bookmarksChanged();
    void currentBookmarkedChanged();
    void paragraphsChanged();
    void originalParagraphsChanged();
    void bilingualSourcesChanged();
    void bilingualTranslationsChanged();
    void bilingualAvailableChanged();
    void chapterTitleChanged();
    void currentIndexChanged();
    void totalChaptersChanged();
    void novelTitleChanged();
    void languageChanged();
    void readingModeChanged();
    void fontSizeChanged();
    void lineSpacingChanged();
    void fontFamilyChanged();
    void maxWidthChanged();
    void readerThemeChanged();

private:
    void loadChapterContent(int index, bool resetVersion);
    QString currentDocumentPath() const;
    QString editedPath(const QString &path) const;
    QString versionPath(const QString &path) const;
    QString catalogTitle(int chapterNumber);
    void refreshChapterFiles();
    int translationIndexForSourceIndex(int sourceIndex) const;
    QStringList readParagraphs(const QString &path, bool isSource) const;
    void saveCurrentChapter();
    void loadNovelProgress();
    void saveNovelProgress();
    QString progressGroup(const QString &novelKey) const;
    QString currentChapterBase() const;
    int chapterIndexForBase(const QString &base) const;
    int paragraphCount() const;
    void restoreChapterPosition();
    void prepareOpen();

    // 仅缓存当前书籍的标题索引，文件变化或重新打开书籍时失效。
    QString m_catalogPath;
    qint64 m_catalogSize = -1;
    QDateTime m_catalogModified;
    QHash<int, QString> m_catalogTitles;
    QString m_sourceDir;
    QString m_translationDir;
    QString m_cjdlDir;
    QString m_cjdlKey;
    QString m_novelTitle;
    QString m_novelKey;
    QString m_language = QStringLiteral("zh"); // jp / zh
    QStringList m_chapterFiles;      // 原文章节文件名（按名称排序）
    QStringList m_translationFiles;  // 译文章节文件名（按名称排序）

    QStringList m_paragraphs;
    QStringList m_originalParagraphs;
    QStringList m_bilingualSources;
    QStringList m_bilingualTranslations;
    bool m_bilingualAvailable = false;
    QString m_chapterTitle;
    int m_currentIndex = 0;
    int m_totalChapters = 0;
    int m_jpIndex = 0;
    int m_zhIndex = 0;
    QSet<int> m_readJp;
    QSet<int> m_readZh;
    QSet<int> m_readBilingual;
    int m_bilingualIndex = 0;
    int m_positionParagraph = 0;
    qreal m_positionOffset = 0;
    bool m_chapterLoaded = false;
    QString m_resumeMode;

    QString m_readingMode = QStringLiteral("translation"); // original / translation / bilingual
    int m_fontSize = 18;
    qreal m_lineSpacing = 1.8;
    QString m_fontFamily = QStringLiteral("Microsoft YaHei UI");
    qreal m_maxWidth = 720;
    QString m_readerTheme = QStringLiteral("light"); // light / sepia / dark / black
    QString m_currentTranslationPath;
    QString m_currentBilingualPath;
    bool m_viewingOriginal = false;
    bool m_editing = false;
    QString m_editorText;
    QString m_initialEditorText;
    QJsonObject m_editorDocument;
    QJsonObject m_initialEditorDocument;
    QByteArray m_editBaseBytes;
    QByteArray m_editVersionBytes;
    bool m_editHadVersion = false;
};

#endif // READERMODEL_H
