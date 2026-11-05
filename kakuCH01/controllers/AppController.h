#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include <QObject>
#include <QColor>
#include <QFont>
#include <QStringList>
#include <QHash>
#include <QVariant>
#include "sakuramanager.h"
#include "kakuyomuservice.h"
#include "sakuratranslator.h"
#include "murasakimanager.h"
#include "murasakitranslator.h"
#include "qwenmanager.h"
#include "qwentranslator.h"
#include "sitebrowser.h"
#include "models/ChapterModel.h"
#include "models/TranslationModel.h"
#include "models/LibraryModel.h"
#include "models/ReaderModel.h"
#include "glossarymanager.h"

// QML 与 C++ 业务层之间的桥接控制器。
class AppController : public QObject
{
    Q_OBJECT

    // ---- Sakura 状态 ----
    Q_PROPERTY(bool sakuraRunning READ sakuraRunning NOTIFY sakuraRunningChanged)
    Q_PROPERTY(bool sakuraStarting READ sakuraStarting NOTIFY sakuraStartingChanged)
    Q_PROPERTY(QString sakuraStatus READ sakuraStatus NOTIFY sakuraStatusChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged)

    // ---- Murasaki 状态 ----
    Q_PROPERTY(bool murasakiRunning READ murasakiRunning NOTIFY murasakiRunningChanged)
    Q_PROPERTY(bool murasakiStarting READ murasakiStarting NOTIFY murasakiStartingChanged)
    Q_PROPERTY(QString murasakiStatus READ murasakiStatus NOTIFY murasakiStatusChanged)
    Q_PROPERTY(QString murasakiModelName READ murasakiModelName NOTIFY murasakiModelNameChanged)
    Q_PROPERTY(QString murasakiServerUrl READ murasakiServerUrl NOTIFY murasakiServerUrlChanged)

    // ---- Qwen 状态 ----
    Q_PROPERTY(bool qwenRunning READ qwenRunning NOTIFY qwenRunningChanged)
    Q_PROPERTY(bool qwenStarting READ qwenStarting NOTIFY qwenStartingChanged)
    Q_PROPERTY(QString qwenStatus READ qwenStatus NOTIFY qwenStatusChanged)
    Q_PROPERTY(QString qwenModelName READ qwenModelName NOTIFY qwenModelNameChanged)
    Q_PROPERTY(QString qwenServerUrl READ qwenServerUrl NOTIFY qwenServerUrlChanged)

    // ---- 下载状态 ----
    Q_PROPERTY(bool fetching READ fetching NOTIFY fetchingChanged)
    Q_PROPERTY(QString workTitle READ workTitle NOTIFY workTitleChanged)
    Q_PROPERTY(QString workAuthor READ workAuthor NOTIFY workAuthorChanged)
    Q_PROPERTY(int chapterCount READ chapterCount NOTIFY chapterCountChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectedCountChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)
    Q_PROPERTY(int downloadCurrent READ downloadCurrent NOTIFY downloadProgressChanged)
    Q_PROPERTY(int downloadTotal READ downloadTotal NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString downloadMessage READ downloadMessage NOTIFY downloadProgressChanged)

    // ---- 翻译状态 ----
    Q_PROPERTY(bool translating READ translating NOTIFY translatingChanged)
    Q_PROPERTY(int translateCurrent READ translateCurrent NOTIFY translateProgressChanged)
    Q_PROPERTY(int translateTotal READ translateTotal NOTIFY translateProgressChanged)
    Q_PROPERTY(QString translateMessage READ translateMessage NOTIFY translateProgressChanged)

    // ---- Murasaki 翻译状态 ----
    Q_PROPERTY(bool murasakiTranslating READ murasakiTranslating NOTIFY murasakiTranslatingChanged)
    Q_PROPERTY(int murasakiTranslateCurrent READ murasakiTranslateCurrent NOTIFY murasakiTranslateProgressChanged)
    Q_PROPERTY(int murasakiTranslateTotal READ murasakiTranslateTotal NOTIFY murasakiTranslateProgressChanged)
    Q_PROPERTY(QString murasakiTranslateMessage READ murasakiTranslateMessage NOTIFY murasakiTranslateProgressChanged)

    // ---- Qwen 翻译状态 ----
    Q_PROPERTY(bool qwenTranslating READ qwenTranslating NOTIFY qwenTranslatingChanged)
    Q_PROPERTY(int qwenTranslateCurrent READ qwenTranslateCurrent NOTIFY qwenTranslateProgressChanged)
    Q_PROPERTY(int qwenTranslateTotal READ qwenTranslateTotal NOTIFY qwenTranslateProgressChanged)
    Q_PROPERTY(QString qwenTranslateMessage READ qwenTranslateMessage NOTIFY qwenTranslateProgressChanged)

    // ---- Qwen 术语表提取状态 ----
    Q_PROPERTY(bool qwenExtracting READ qwenExtracting NOTIFY qwenExtractingChanged)
    Q_PROPERTY(int qwenExtractCurrent READ qwenExtractCurrent NOTIFY qwenExtractProgressChanged)
    Q_PROPERTY(int qwenExtractTotal READ qwenExtractTotal NOTIFY qwenExtractProgressChanged)
    Q_PROPERTY(QString qwenExtractMessage READ qwenExtractMessage NOTIFY qwenExtractProgressChanged)
    Q_PROPERTY(QString qwenExtractLabel READ qwenExtractLabel NOTIFY qwenExtractProgressChanged)

    // ---- 概要翻译（网站详情右侧）----
    Q_PROPERTY(bool siteOverviewTranslating READ siteOverviewTranslating NOTIFY siteOverviewTranslatingChanged)
    Q_PROPERTY(int siteOverviewCurrent READ siteOverviewCurrent NOTIFY siteOverviewProgressChanged)
    Q_PROPERTY(int siteOverviewTotal READ siteOverviewTotal NOTIFY siteOverviewProgressChanged)
    Q_PROPERTY(QString siteOverviewMessage READ siteOverviewMessage NOTIFY siteOverviewProgressChanged)
    Q_PROPERTY(bool siteOverviewTranslated READ siteOverviewTranslated NOTIFY siteOverviewTranslatedChanged)
    Q_PROPERTY(QString siteOverviewTranslation READ siteOverviewTranslation NOTIFY siteOverviewTranslatedChanged)
    Q_PROPERTY(QString siteTranslationKind READ siteTranslationKind NOTIFY siteOverviewTranslatingChanged)
    Q_PROPERTY(bool siteCommentsTranslated READ siteCommentsTranslated NOTIFY siteCommentsChanged)
    Q_PROPERTY(QStringList siteCommentTranslations READ siteCommentTranslations NOTIFY siteCommentsChanged)

    // ---- 「网站」在线浏览 ----
    Q_PROPERTY(bool siteBusy READ siteBusy NOTIFY siteBusyChanged)
    // 只有「检索 / 翻页」这类检索请求为 true；打开作品、拉排行榜不算，
    // 否则点开一本小说时检索按钮会显示成「检索中…」
    Q_PROPERTY(bool siteSearching READ siteSearching NOTIFY siteSearchingChanged)
    Q_PROPERTY(QString siteMessage READ siteMessage NOTIFY siteMessageChanged)
    Q_PROPERTY(QString siteTitle READ siteTitle NOTIFY siteWorkChanged)
    Q_PROPERTY(QString siteAuthor READ siteAuthor NOTIFY siteWorkChanged)
    Q_PROPERTY(QString siteName READ siteName NOTIFY siteWorkChanged)
    Q_PROPERTY(QString siteWorkUrl READ siteWorkUrl NOTIFY siteWorkChanged)
    Q_PROPERTY(int siteChapterCount READ siteChapterCount NOTIFY siteWorkChanged)
    Q_PROPERTY(int siteChapterRevision READ siteChapterRevision NOTIFY siteWorkChanged)
    Q_PROPERTY(QVariantList siteCards READ siteCards NOTIFY siteCardsChanged)
    Q_PROPERTY(QString siteListTitle READ siteListTitle NOTIFY siteCardsChanged)
    Q_PROPERTY(int siteLengthFilterValue READ siteLengthFilterValue NOTIFY siteCardsChanged)
    Q_PROPERTY(QString siteMode READ siteMode NOTIFY siteModeChanged)
    Q_PROPERTY(QVariantList siteSearchGenres READ siteSearchGenres CONSTANT)
    Q_PROPERTY(QStringList siteSearchGenresSelected READ siteSearchGenresSelected
               NOTIFY siteSearchStateChanged)
    Q_PROPERTY(QString siteKeyword READ siteKeyword NOTIFY siteSearchStateChanged)
    Q_PROPERTY(QString siteSearchKind READ siteSearchKind NOTIFY siteSearchStateChanged)
    Q_PROPERTY(int sitePage READ sitePage NOTIFY sitePageChanged)
    Q_PROPERTY(int sitePageCount READ sitePageCount NOTIFY sitePageChanged)
    Q_PROPERTY(QString siteInclusions READ siteInclusions NOTIFY siteSearchStateChanged)
    Q_PROPERTY(QString siteExclusions READ siteExclusions NOTIFY siteSearchStateChanged)
    Q_PROPERTY(int siteStatusFilter READ siteStatusFilter NOTIFY siteSearchStateChanged)
    Q_PROPERTY(int siteMinStars READ siteMinStars NOTIFY siteSearchStateChanged)
    Q_PROPERTY(QString siteSearchOrder READ siteSearchOrder NOTIFY siteSearchStateChanged)
    Q_PROPERTY(QVariantMap siteStats READ siteStats NOTIFY siteWorkChanged)
    Q_PROPERTY(QVariantList siteChaptersList READ siteChaptersList NOTIFY siteWorkChanged)
    Q_PROPERTY(QVariantList siteComments READ siteComments NOTIFY siteCommentsChanged)
    Q_PROPERTY(bool siteCommentsBusy READ siteCommentsBusy NOTIFY siteCommentsChanged)
    Q_PROPERTY(bool novelUpdateChecking READ novelUpdateChecking NOTIFY novelUpdateCheckingChanged)
    Q_PROPERTY(bool novelUpdating READ novelUpdating NOTIFY novelUpdatingChanged)
    Q_PROPERTY(int novelUpdateCurrent READ novelUpdateCurrent NOTIFY novelUpdateProgressChanged)
    Q_PROPERTY(int novelUpdateTotal READ novelUpdateTotal NOTIFY novelUpdateProgressChanged)
    Q_PROPERTY(QString novelUpdateMessage READ novelUpdateMessage NOTIFY novelUpdateProgressChanged)
    Q_PROPERTY(bool siteFavorited READ siteFavorited NOTIFY sitePrefsChanged)
    Q_PROPERTY(int siteRating READ siteRating NOTIFY sitePrefsChanged)
    Q_PROPERTY(QString pendingDownloadUrl READ pendingDownloadUrl NOTIFY pendingDownloadUrlChanged)

    // ---- 子对象 ----
    Q_PROPERTY(ChapterModel *chapterModel READ chapterModel CONSTANT)
    Q_PROPERTY(TranslationModel *translationModel READ translationModel CONSTANT)
    Q_PROPERTY(LibraryModel *libraryModel READ libraryModel CONSTANT)
    Q_PROPERTY(ReaderModel *readerModel READ readerModel CONSTANT)

    // ---- 当前选中小说 ----
    Q_PROPERTY(int currentNovelRow READ currentNovelRow NOTIFY currentNovelRowChanged)

    // ---- 翻译设置 ----
    Q_PROPERTY(QString systemPrompt READ systemPrompt NOTIFY systemPromptChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController();

    bool sakuraRunning() const;
    bool sakuraStarting() const;
    QString sakuraStatus() const;
    QString modelName() const;
    QString serverUrl() const;

    bool murasakiRunning() const;
    bool murasakiStarting() const;
    QString murasakiStatus() const;
    QString murasakiModelName() const;
    QString murasakiServerUrl() const;

    bool qwenRunning() const;
    bool qwenStarting() const;
    QString qwenStatus() const;
    QString qwenModelName() const;
    QString qwenServerUrl() const;

    bool fetching() const;
    QString workTitle() const;
    QString workAuthor() const;
    int chapterCount() const;
    int selectedCount() const;
    bool downloading() const;
    int downloadCurrent() const { return m_downloadCurrent; }
    int downloadTotal() const { return m_downloadTotal; }
    QString downloadMessage() const { return m_downloadMessage; }

    bool translating() const;
    int translateCurrent() const { return m_translateCurrent; }
    int translateTotal() const { return m_translateTotal; }
    QString translateMessage() const { return m_translateMessage; }

    bool murasakiTranslating() const;
    int murasakiTranslateCurrent() const { return m_murasakiTranslateCurrent; }
    int murasakiTranslateTotal() const { return m_murasakiTranslateTotal; }
    QString murasakiTranslateMessage() const { return m_murasakiTranslateMessage; }

    bool qwenTranslating() const;
    int qwenTranslateCurrent() const { return m_qwenTranslateCurrent; }
    int qwenTranslateTotal() const { return m_qwenTranslateTotal; }
    QString qwenTranslateMessage() const { return m_qwenTranslateMessage; }

    bool qwenExtracting() const;
    int qwenExtractCurrent() const { return m_qwenExtractCurrent; }
    int qwenExtractTotal() const { return m_qwenExtractTotal; }
    QString qwenExtractMessage() const { return m_qwenExtractMessage; }
    QString qwenExtractLabel() const { return m_qwenExtractLabel; }

    bool siteOverviewTranslating() const { return m_siteOverviewTranslating; }
    int siteOverviewCurrent() const { return m_siteOverviewCurrent; }
    int siteOverviewTotal() const { return m_siteOverviewTotal; }
    QString siteOverviewMessage() const { return m_siteOverviewMessage; }
    bool siteOverviewTranslated() const { return m_siteOverviewTranslated; }
    QString siteOverviewTranslation() const { return m_siteOverviewTranslation; }
    QString siteTranslationKind() const { return m_siteTranslationKind; }
    bool siteCommentsTranslated() const { return m_siteCommentsTranslated; }
    QStringList siteCommentTranslations() const { return m_siteCommentTranslations; }

    bool siteBusy() const { return m_siteBusy; }
    bool siteSearching() const { return m_siteSearching; }
    QString siteMessage() const { return m_siteMessage; }
    QString siteTitle() const { return m_siteWork.title; }
    QString siteAuthor() const { return m_siteWork.author; }
    QString siteName() const;
    QString siteWorkUrl() const { return m_siteWork.workUrl; }
    int siteChapterCount() const { return m_siteWork.chapters.size(); }
    int siteChapterRevision() const { return m_siteChapterRevision; }
    QVariantList siteCards() const;
    Q_INVOKABLE QVariantList siteCardsSnapshot() const { return siteCards(); }
    QString siteListTitle() const { return m_siteListTitle; }
    int siteLengthFilterValue() const { return m_siteMode == QStringLiteral("ranking") ? m_siteRankLengthFilter : m_siteLengthFilter; }
    QString siteMode() const { return m_siteMode; }
    QVariantList siteSearchGenres() const;
    QStringList siteSearchGenresSelected() const { return m_siteSearchGenresSelected; }
    QString siteKeyword() const { return m_siteKeyword; }
    QString siteSearchKind() const { return m_siteSearchKind; }
    QString siteInclusions() const { return m_siteInclusions; }
    QString siteExclusions() const { return m_siteExclusions; }
    int siteStatusFilter() const { return m_siteMode == QStringLiteral("ranking") ? m_siteRankStatusFilter : m_siteStatusFilter; }
    QString siteSearchOrder() const { return m_siteSearchOrder; }
    int siteMinStars() const { return m_siteMinStars; }
    int sitePage() const { return m_sitePage; }
    int sitePageCount() const { return m_sitePageCount; }
    QVariantMap siteStats() const { return m_siteStats; }
    QVariantList siteChaptersList() const { return m_siteChaptersList; }
    QVariantList siteComments() const { return m_siteComments; }
    bool siteCommentsBusy() const { return m_siteCommentsBusy; }
    bool siteFavorited() const { return m_siteFavorited; }
    int siteRating() const { return m_siteRating; }
    QString pendingDownloadUrl() const { return m_pendingDownloadUrl; }

    ChapterModel *chapterModel() const;
    TranslationModel *translationModel() const;
    LibraryModel *libraryModel() const;
    ReaderModel *readerModel() const;

    SakuraManager *sakuraManager() const { return m_sakura; }
    SakuraTranslator *translator() const { return m_translator; }
    MurasakiManager *murasakiManager() const { return m_murasaki; }
    MurasakiTranslator *murasakiTranslator() const { return m_murasakiTranslator; }
    QwenManager *qwenManager() const { return m_qwen; }
    QwenTranslator *qwenTranslator() const { return m_qwenTranslator; }

    Q_INVOKABLE QString downloadDir() const;
    Q_INVOKABLE QString chdlDir() const;
    Q_INVOKABLE QString cjdlDir() const;
    Q_INVOKABLE QString saveRoot() const;
    Q_INVOKABLE void openUserTranslationDirectory();
    Q_INVOKABLE QString defaultSaveRoot() const;
    Q_INVOKABLE bool generateBilingual() const;
    Q_INVOKABLE QString sakuraDir() const;
    Q_INVOKABLE QString modelDir() const;
    Q_INVOKABLE QStringList availableModels() const;
    Q_INVOKABLE QVariantMap modelGpuConfiguration(const QString &engine, const QString &modelPath) const;
    Q_INVOKABLE bool saveModelGpuConfiguration(const QString &engine, const QString &modelPath, int layers);
    Q_INVOKABLE int requestModelGpuRecommendation(const QString &engine, const QString &modelPath);
    Q_INVOKABLE QString murasakiDir() const;
    Q_INVOKABLE QStringList murasakiAvailableModels() const;
    Q_INVOKABLE QString murasakiQualityPrompt() const;
    Q_INVOKABLE QString qwenDir() const;
    Q_INVOKABLE QStringList qwenAvailableModels() const;
    Q_INVOKABLE QString qwenQualityPrompt() const;
    Q_INVOKABLE QString qwenGlossaryPrompt() const;

    int currentNovelRow() const { return m_currentNovelRow; }
    Q_INVOKABLE QString currentNovelTitle() const;
    Q_INVOKABLE QString currentNovelAuthor() const;
    Q_INVOKABLE QString currentNovelWorkId() const;
    // 作品页地址（用于界面显示 / 点击打开浏览器）
    Q_INVOKABLE QString currentNovelUrl() const;
    // 已阅读（中文目录）章节数，与阅读进度同一份数据
    Q_INVOKABLE int currentNovelReadCount() const;
    // 检查当前小说是否有更新（远端章节 vs 本地章节文件）
    Q_INVOKABLE void checkCurrentNovelUpdate();
    // 下载缺失章节（检查更新判定为不完整后调用）
    Q_INVOKABLE void updateCurrentNovel();
    // 把当前小说的全部章节加入翻译队列并全选（QML 之后跳到翻译页）
    Q_INVOKABLE void queueCurrentNovelForTranslation();
    // 删除整本书（原文目录 / 译文 / 双语资源 / 术语表 / 概要翻译 / 阅读进度）
    Q_INVOKABLE bool deleteCurrentNovel();
    bool novelUpdateChecking() const { return m_novelUpdateChecking; }
    bool novelUpdating() const { return m_novelUpdating; }
    int novelUpdateCurrent() const { return m_novelUpdateCurrent; }
    int novelUpdateTotal() const { return m_novelUpdateTotal; }
    QString novelUpdateMessage() const { return m_novelUpdateMessage; }
    Q_INVOKABLE QString currentNovelRootPath() const;
    Q_INVOKABLE int currentNovelChapterCount() const;
    Q_INVOKABLE int currentNovelTranslatedCount() const;
    Q_INVOKABLE int currentNovelDownloadedCount() const;
    Q_INVOKABLE QStringList currentNovelChapterNames() const;
    Q_INVOKABLE QString currentNovelChapterTitle(int chapterIndex) const;
    Q_INVOKABLE bool currentNovelChapterTranslated(int chapterIndex) const;
    Q_INVOKABLE bool currentNovelChapterRead(int chapterIndex, const QString &language) const;
    Q_INVOKABLE QStringList currentNovelBilingualChapterNames() const;
    Q_INVOKABLE bool currentNovelBilingualChapterAvailable(const QString &chapterBaseName) const;

    // ---- 小说级手动术语表（Reader 右键添加/编辑） ----
    Q_INVOKABLE QVariantList currentNovelGlossaryTerms() const;
    Q_INVOKABLE bool currentNovelGlossaryContains(const QString &source) const;
    Q_INVOKABLE QVariantMap currentNovelGlossaryTerm(const QString &source) const;
    Q_INVOKABLE bool addCurrentNovelGlossaryTerm(const QString &source,
                                                 const QString &target,
                                                 const QString &type,
                                                 const QString &note,
                                                 bool enabled);
    Q_INVOKABLE bool updateCurrentNovelGlossaryTerm(const QString &oldSource,
                                                    const QString &source,
                                                    const QString &target,
                                                    const QString &type,
                                                    const QString &note,
                                                    bool enabled);
    Q_INVOKABLE bool removeCurrentNovelGlossaryTerm(const QString &source);
    Q_INVOKABLE bool setCurrentNovelGlossaryEnabled(const QString &source, bool enabled);

    // ---- 两份术语表（人工 / Qwen）：查看、编辑与「翻译时启用哪一份」 ----
    Q_INVOKABLE QString activeGlossaryKind() const;                 // "manual" / "qwen"
    Q_INVOKABLE void setActiveGlossaryKind(const QString &kind);
    Q_INVOKABLE bool glossaryExistsIn(const QString &kind) const;
    Q_INVOKABLE int glossaryCountIn(const QString &kind) const;
    Q_INVOKABLE QVariantList glossaryTermsIn(const QString &kind) const;
    Q_INVOKABLE QVariantMap glossaryTermIn(const QString &kind, const QString &source) const;
    Q_INVOKABLE bool addGlossaryTermIn(const QString &kind, const QString &source,
                                       const QString &target, const QString &type,
                                       const QString &note, bool enabled);
    Q_INVOKABLE bool updateGlossaryTermIn(const QString &kind, const QString &oldSource,
                                          const QString &source, const QString &target,
                                          const QString &type, const QString &note,
                                          bool enabled);
    Q_INVOKABLE bool removeGlossaryTermIn(const QString &kind, const QString &source);
    // 删除整份术语表（只删指定的一份："manual" 或 "qwen"）
    Q_INVOKABLE bool clearGlossaryIn(const QString &kind);
    // 把一条术语复制到另一份术语表（人工 ↔ Qwen）
    Q_INVOKABLE bool copyGlossaryTermTo(const QString &fromKind, const QString &toKind,
                                        const QString &source, bool overwrite = false);
    Q_INVOKABLE bool setGlossaryTermEnabledIn(const QString &kind, const QString &source,
                                              bool enabled);
    Q_INVOKABLE void startQwenGlossaryExtraction();
    Q_INVOKABLE void cancelQwenGlossaryExtraction();

    // ---- 「网站」在线浏览 ----
    Q_INVOKABLE void siteSearch(const QString &keyword);
    Q_INVOKABLE void siteRefresh();
    Q_INVOKABLE void siteRanking(const QString &genre, const QString &period);
    Q_INVOKABLE QVariantList sitePeriods() const;
    Q_INVOKABLE QVariantList siteLengths() const;
    Q_INVOKABLE void siteSetLengthFilter(int value);
    Q_INVOKABLE void siteSetMode(const QString &mode);
    Q_INVOKABLE void siteSetKeyword(const QString &text);
    Q_INVOKABLE void siteSetSearchKind(const QString &kind);
    Q_INVOKABLE void siteSetInclusions(const QString &text);
    Q_INVOKABLE void siteSetExclusions(const QString &text);
    Q_INVOKABLE void siteSetStatusFilter(int value);
    Q_INVOKABLE void siteSetMinStars(int value);
    Q_INVOKABLE void siteSetSearchOrder(const QString &order);
    Q_INVOKABLE void siteToggleSearchGenre(const QString &jp, bool on);
    Q_INVOKABLE void siteToggleSearchKeyword(const QString &jp, bool on);
    Q_INVOKABLE void siteGoPage(int page);
    Q_INVOKABLE void siteSearchNow();
    Q_INVOKABLE void siteOpenWork(const QString &url);
    Q_INVOKABLE void siteLoadComments();
    Q_INVOKABLE void siteDownloadWork();
    // 供 QML 在弹窗之前先判是否已有其它翻译在跑
    Q_INVOKABLE bool translationBusy() const { return anyTranslateBusy(); }
    Q_INVOKABLE void siteTranslateOverview();
    Q_INVOKABLE void siteDeleteOverviewTranslation();
    Q_INVOKABLE void siteTranslateComments();
    Q_INVOKABLE void siteDeleteCommentsTranslation();
    Q_INVOKABLE void clearAllOverviewTranslations();
    Q_INVOKABLE void clearAllCommentsTranslations();
    Q_INVOKABLE QVariantList siteGenres() const;
    Q_INVOKABLE void siteToggleFavorite();
    Q_INVOKABLE void siteSetRating(int stars);
    Q_INVOKABLE void queueSiteWorkForDownload();
    Q_INVOKABLE void clearPendingDownloadUrl();
    Q_INVOKABLE QVariantList supportedSites() const;
    Q_INVOKABLE QString userGuideText() const;
    Q_INVOKABLE QString userGuideHtml(const QColor &linkColor, const QColor &borderColor,
                                     const QFont &font) const;

    QString systemPrompt() const { return m_systemPrompt; }

    Q_INVOKABLE int libraryNovelCount() const;
    Q_INVOKABLE int libraryDownloadedCount() const;
    Q_INVOKABLE int libraryTranslatedCount() const;

public slots:
    // Sakura
    void startSakura();
    void stopSakura();
    void browseModelDir(const QString &dir);
    void refreshModels();
    void setModelPath(const QString &path);
    void setModelDir(const QString &dir);
    void setSakuraDir(const QString &dir);
    void startMurasaki();
    void stopMurasaki();
    void refreshMurasakiModels();
    void setMurasakiModelPath(const QString &path);
    void setMurasakiDir(const QString &dir);
    void startQwen();
    void stopQwen();
    void refreshQwenModels();
    void setQwenModelPath(const QString &path);
    void setQwenDir(const QString &dir);
    void setDownloadDir(const QString &dir);
    void setChdlDir(const QString &dir);
    void setSaveRoot(const QString &dir);
    void resetSaveRoot();
    void setGenerateBilingual(bool enabled);

    // 下载
    void fetchWork(const QString &url);
    Q_INVOKABLE void clearDownloadWork();
    void downloadSelected(const QString &dir);
    void cancelDownload();
    void setAllSelected(bool selected);
    Q_INVOKABLE void invertSelection();
    void setChapterFilter(const QString &filter);

    // 翻译
    void addFiles(const QStringList &paths);
    void addFolder(const QString &dir);
    void removeSelectedFiles(bool glossaryMode = false);
    void setTranslationSelection(bool selected, bool glossaryMode = false);
    void invertTranslationSelection(bool glossaryMode = false);
    int addLibraryBookToQueue(int displayRow);
    void clearFiles();
    void startTranslate(const QString &outputDir);
    void stopTranslate();
    void startMurasakiTranslate(const QString &outputDir);
    void stopMurasakiTranslate();
    void startQwenTranslate(const QString &outputDir);
    void stopQwenTranslate();

    // 书库 / 阅读
    void refreshLibrary();
    void openNovelForReading(int libraryRow);
    void openNovelChapter(int chapterIndex, const QString &language);
    Q_INVOKABLE void openNovelChapterFile(const QString &chapterBaseName,
                                          const QString &language);
    Q_INVOKABLE void openNovelBilingualChapterFile(const QString &chapterBaseName);
    void clearCurrentNovelReadingProgress(const QString &mode);
    void generateBilingualResources();
    void deleteBilingualResource(const QString &chapterBaseName);
    void clearBilingualResources();
    void selectNovel(int libraryRow);
    void openCurrentNovelForReading();
    void queueChapterForTranslation(int chapterIndex);
    void deleteChapterTranslation(int chapterIndex);
    void setSystemPrompt(const QString &prompt);
    void applyPromptPreset(const QString &preset);

    void shutdown();

signals:
    void modelGpuRecommendationReady(int requestId, const QVariantMap &result);
    void sakuraRunningChanged();
    void sakuraStartingChanged();
    void sakuraStatusChanged();
    void modelNameChanged();
    void serverUrlChanged();

    void murasakiRunningChanged();
    void murasakiStartingChanged();
    void murasakiStatusChanged();
    void murasakiModelNameChanged();
    void murasakiServerUrlChanged();

    void qwenRunningChanged();
    void qwenStartingChanged();
    void qwenStatusChanged();
    void qwenModelNameChanged();
    void qwenServerUrlChanged();

    void fetchingChanged();
    void workTitleChanged();
    void workAuthorChanged();
    void chapterCountChanged();
    void selectedCountChanged();
    void downloadingChanged();
    void downloadProgressChanged();

    void translatingChanged();
    void translateProgressChanged();

    void murasakiTranslatingChanged();
    void murasakiTranslateProgressChanged();

    void qwenTranslatingChanged();
    void qwenTranslateProgressChanged();
    void qwenExtractingChanged();
    void qwenExtractProgressChanged();

    void siteOverviewTranslatingChanged();
    void siteOverviewProgressChanged();
    void siteOverviewTranslatedChanged();

    void siteBusyChanged();
    void siteSearchingChanged();
    void siteMessageChanged();
    void siteWorkChanged();
    void siteCardsChanged();
    void siteCommentsChanged();
    void sitePrefsChanged();
    void siteModeChanged();
    void siteSearchStateChanged();
    void sitePageChanged();
    void pendingDownloadUrlChanged();

    void notify(const QString &message);
    void workReady();
    void workFailed(const QString &error);
    void downloadProgress(int current, int total, const QString &message);
    void downloadFinished(int success, int failed);

    void openReaderRequested();
    void novelSelected();
    void currentNovelRowChanged();
    void libraryRefreshed();
    void systemPromptChanged();
    void saveRootChanged();
    void generateBilingualChanged();
    void glossaryChanged();
    // 检查更新结果：state = "complete" / "incomplete" / "failed"
    void novelUpdateChecked(const QString &state, int missingCount);
    // 更新章节结束：addedCount 为本次新增章节数
    void novelUpdateFinished(bool ok, int addedCount);
    void novelUpdateCheckingChanged();
    void novelUpdatingChanged();
    void novelUpdateProgressChanged();

private slots:
    void onSakuraStarted();
    void onSakuraStopped();
    void onSakuraFailed(const QString &error);
    void onSakuraLog(const QString &message);
    void onWorkReady(const NovelWork &work);
    void onWorkFailed(const QString &error);
    void onDownloadProgress(int current, int total, const QString &message);
    void onDownloadFinished(int success, int failed);
    void onTranslateProgress(int current, int total, const QString &message);
    void onTranslateFileStatus(const QString &path, const QString &status);
    void onTranslateFinished(int success, int failed);
    void onMurasakiStarted();
    void onMurasakiStopped();
    void onMurasakiFailed(const QString &error);
    void onMurasakiLog(const QString &message);
    void onMurasakiTranslateProgress(int current, int total, const QString &message);
    void onMurasakiTranslateFileStatus(const QString &path, const QString &status);
    void onMurasakiTranslateFinished(int success, int failed);
    void onQwenStarted();
    void onQwenStopped();
    void onQwenFailed(const QString &error);
    void onQwenLog(const QString &message);
    void onQwenTranslateProgress(int current, int total, const QString &message);
    void onQwenTranslateFileStatus(const QString &path, const QString &status);
    void onQwenTranslateFinished(int success, int failed);
    void onQwenGlossaryProgress(int current, int total, const QString &message);
    void onQwenGlossaryFinished(int termCount, const QString &message);
    void onUpdateWorkReady(const NovelWork &work);
    void onUpdateWorkFailed(const QString &error);
    void onUpdateDownloadFinished(int successCount, int failedCount);
    void onUpdateProgress(int current, int total, const QString &message);
    void onSiteWorkOpened(const NovelWork &work);
    void onSiteWorkStats(const QVariantMap &stats);
    void onSiteWorkFailed(const QString &error);
    void onSiteCardsReady(const QVariantList &cards);
    void onSiteCardsFailed(const QString &error);
    void onSitePageReady(int totalPages);
    void onSiteCommentsReady(const QVariantList &comments);
    void onSiteCommentsFailed(const QString &error);
    void onSiteAuthorResolved(const QString &authorName);

private:
    QString workspaceRootDir() const;
    void updateSelectedCount();
    void updateModelUi();
    void refreshTranslationQueueStatuses();
    void updateTranslationFileStatuses(const QStringList &files, const QStringList &outputDirs);
    QString novelRootForSourceFile(const QString &path) const;
    QString currentNovelKey() const;
    void applyDerivedDirs();
    GlossaryManager loadCurrentGlossary() const;
    bool saveGlossaryChanges(GlossaryManager &glossary, const QString &message);
    KakuyomuService *m_updateService = nullptr;
    NovelWork m_updateWork;
    QVector<Chapter> m_updateMissing;
    bool m_novelUpdateChecking = false;
    bool m_novelUpdating = false;
    int m_novelUpdateCurrent = 0;
    int m_novelUpdateTotal = 0;
    QString m_novelUpdateMessage;
    GlossaryManager loadGlossaryOfKind(const QString &kind) const;
    // 目录布局（DOWNLOAD/CHDL/CJDL/<siteKey>/<workId>）相关辅助
    void migrateLegacyLayouts();
    QString relativeKeyForSourceFile(const QString &path) const;
    bool isUserTranslationSource(const QString &path) const;
    QString translationOutputDir(const QString &path, const QString &defaultDir,
                                 const QHash<QString, QString> &userOutputs) const;
    bool prepareUserTranslationOutputs(const QStringList &files, QHash<QString, QString> *outputs);
    QString currentNovelRelativeKey() const;
    QString novelRootForWork(const NovelWork &work) const;
    QVariantList buildSiteChapterList() const;
    bool anyTranslateBusy() const;
    QString siteOverviewDir() const;
    QString siteOverviewPathFor(const QString &siteKey, const QString &workId) const;
    QString siteOverviewFilePath() const;
    QString siteCommentsDir() const;
    QString siteCommentsPathFor(const QString &siteKey, const QString &workId) const;
    void startSiteTextTranslation(const QString &kind, const QStringList &sources);
    void refreshSiteOverviewTranslation();
    void refreshSiteCommentsTranslation();
    void finishSiteOverviewTranslation(int success);

    SakuraManager *m_sakura;
    MurasakiManager *m_murasaki;
    QwenManager *m_qwen;
    SiteBrowser *m_siteBrowser;
    KakuyomuService *m_service;
    SakuraTranslator *m_translator;
    MurasakiTranslator *m_murasakiTranslator;
    QwenTranslator *m_qwenTranslator;
    ChapterModel *m_chapterModel;
    TranslationModel *m_translationModel;
    LibraryModel *m_libraryModel;
    ReaderModel *m_readerModel;

    NovelWork m_work;
    int m_currentNovelRow = -1;
    QString m_systemPrompt;
    int m_modelGpuRequestId = 0;

    bool m_fetching = false;
    bool m_downloading = false;
    bool m_translating = false;
    bool m_sakuraRunning = false;
    bool m_sakuraStarting = false;
    QString m_sakuraStatus = QStringLiteral("未运行");
    bool m_murasakiRunning = false;
    bool m_murasakiStarting = false;
    QString m_murasakiStatus = QStringLiteral("未运行");
    QString m_workTitle;
    QString m_workAuthor;
    QString m_downloadDir;
    QString m_chdlDir;
    QString m_cjdlDir;
    QString m_saveRoot;
    QString m_sakuraDir;
    QString m_modelDir;
    QString m_murasakiDir;
    QString m_qwenDir;
    bool m_generateBilingual = false;

    int m_downloadCurrent = 0;
    int m_downloadTotal = 0;
    QString m_downloadMessage;
    int m_translateCurrent = 0;
    int m_translateTotal = 0;
    QString m_translateMessage;
    QStringList m_translateFiles;
    QStringList m_translateOutputDirs;
    bool m_murasakiTranslating = false;
    int m_murasakiTranslateCurrent = 0;
    int m_murasakiTranslateTotal = 0;
    QString m_murasakiTranslateMessage;
    bool m_qwenRunning = false;
    bool m_qwenStarting = false;
    QString m_qwenStatus = QStringLiteral("未运行");
    bool m_qwenTranslating = false;
    int m_qwenTranslateCurrent = 0;
    int m_qwenTranslateTotal = 0;
    QString m_qwenTranslateMessage;
    QStringList m_qwenTranslateFiles;
    QStringList m_qwenTranslateOutputDirs;
    bool m_qwenExtracting = false;
    int m_qwenExtractCurrent = 0;
    int m_qwenExtractTotal = 0;
    QString m_qwenExtractMessage;
    QString m_qwenExtractLabel = QStringLiteral("提取术语");

    // ---- 「网站」在线浏览 ----
    bool m_siteBusy = false;
    bool m_siteSearching = false;
    QString m_siteMessage;
    NovelWork m_siteWork;
    QString m_siteRefreshWorkUrl;
    int m_siteChapterRevision = 0;
    QVariantList m_siteCards;
    int m_siteLengthFilter = 0;
    int m_siteRankLengthFilter = 0;
    int m_siteRankStatusFilter = 0;
    QString m_siteRankGenre = QStringLiteral("all");
    QString m_siteRankPeriod = QStringLiteral("weekly");
    QString m_siteSearchOrder = QStringLiteral("weekly_ranking");
    QString m_siteSubmittedKeyword;
    QString m_siteMode = QStringLiteral("ranking");
    QString m_siteKeyword;
    QString m_siteSearchKind = QStringLiteral("works");
    QStringList m_siteSearchGenresSelected;
    QString m_siteInclusions;
    QString m_siteExclusions;
    int m_siteStatusFilter = 0;
    int m_siteMinStars = 0;
    int m_sitePage = 1;
    int m_sitePageCount = 1;
    QString m_siteListTitle;
    QVariantMap m_siteStats;
    QVariantList m_siteChaptersList;
    QVariantList m_siteComments;
    QStringList m_siteCommentTranslations;
    bool m_siteCommentsTranslated = false;
    bool m_siteCommentsBusy = false;
    bool m_siteFavorited = false;
    int m_siteRating = 0;

    // 概要翻译状态
    bool m_siteOverviewTranslating = false;
    int m_siteOverviewCurrent = 0;
    int m_siteOverviewTotal = 1;
    QString m_siteOverviewMessage;
    bool m_siteOverviewTranslated = false;
    QString m_siteOverviewTranslation;
    int m_siteOverviewModel = -1;
    QString m_siteTranslationKind;
    QStringList m_siteTranslationSources;
    QStringList m_siteTranslationFiles;
    QString m_siteOverviewSourceDir;
    QString m_siteOverviewOutputDir;
    // 概要翻译启动时锁定的作品身份：翻译期间用户可能切到别的小说，
    // 落盘与界面回填都必须认这本书，而不是当前打开的那本。
    QString m_siteOverviewSiteKey;
    QString m_siteOverviewWorkId;
    QString m_pendingDownloadUrl;
};

#endif // APPCONTROLLER_H
