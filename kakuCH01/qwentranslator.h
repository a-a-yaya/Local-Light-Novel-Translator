#ifndef QWENTRANSLATOR_H
#define QWENTRANSLATOR_H

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QList>
#include <QSet>
#include "httpclient.h"
#include "glossarymanager.h"

// 单个 Qwen 翻译任务：源文件路径 + 输出目录
struct QwenFileJob
{
    QString path;
    QString outputDir;
};

// Qwen 短期上下文：原文 + 译文
struct QwenHistoryEntry
{
    QString sourceText;
    QString translatedText;
};

// 独立的 Qwen 翻译流程（与 Sakura / Murasaki 不共享实现）：
//  - 使用通用模型的轻小说翻译 Prompt（非推理模型，思考模式关闭）；
//  - 支持小说级术语表注入与前后文 History；
//  - 额外提供「只提取术语表、不翻译」的术语表生成功能。
class QwenTranslator : public QObject
{
    Q_OBJECT
public:
    explicit QwenTranslator(QObject *parent = nullptr);

    static QString defaultPrompt();
    static QString glossaryPrompt();
    static QString glossaryReviewPrompt();

    void setServerUrl(const QString &url);
    void setModelAlias(const QString &alias);
    void setSystemPrompt(const QString &prompt);
    void setCjdlDir(const QString &dir);
    // 下载根目录：用来算「站点/作品」相对标识，保证「启用哪份术语表」的键
    // 与 AppController 完全一致
    void setDownloadRoot(const QString &dir);

    void translateFiles(const QVector<QwenFileJob> &jobs);
    void abort();

    // 术语表提取：读取所选章节正文，产出 Qwen 术语表，不写任何译文。
    void extractGlossary(const QStringList &files);
    void abortGlossary();

signals:
    void progressChanged(int current, int total, const QString &message);
    void fileStatusChanged(const QString &path, const QString &status);
    void translationFinished(int successCount, int failedCount);

    void glossaryProgressChanged(int current, int total, const QString &message);
    void glossaryStageChanged(const QString &label);
    void glossaryFinished(int termCount, const QString &message);

private slots:
    void onTranslateReply(const QString &url, const QVariant &context,
                          const QByteArray &data, bool ok, const QString &error);
    void onGlossaryReply(const QString &url, const QVariant &context,
                         const QByteArray &data, bool ok, const QString &error);

private:
    struct ChunkTask
    {
        int fileIndex = -1;
        QString text;
    };

    // ---- 翻译 ----
    void buildTaskQueue();
    void translateNextChunk();
    void finishCurrentFile(int fileIndex);
    void loadGlossaries();
    QString glossaryPromptForChunk(int fileIndex, const QString &text) const;
    QList<GlossaryTerm> relevantTermsForChunk(int fileIndex, const QString &text) const;
    QString validateChunk(const ChunkTask &task, const QString &translated,
                          QStringList &missingTerms, bool &japaneseResidue) const;
    void clearHistory();
    void appendHistoryEntry(const QString &sourceText, const QString &translatedText);
    QString serializeHistory(const QList<QwenHistoryEntry> &history) const;
    QList<QwenHistoryEntry> effectiveHistory() const;
    QString extractTranslation(const QByteArray &data) const;
    static QString stripThink(const QString &text);
    static QString stripPromptEcho(const QString &text);
    void resolveSourceIdentity(const QString &filePath,
                               QString &workId, QString &chapterId) const;
    // 章节文件 -> 小说根目录（".../chapters/" 的上层）；非规范结构返回空
    QString novelRootForSourceFile(const QString &filePath) const;

    // ---- 术语表提取 ----
    struct GlossaryChunk
    {
        QString novelRoot;
        QString fileName;
        QString text;
    };

    void buildGlossaryTasks(const QStringList &files);
    void runNextGlossaryTask();
    void runNextGlossaryReview();
    void saveGlossaryResults();
    void requestGlossaryReview(int rootIndex);
    static QString parseJsonArrayText(const QString &raw);
    void collectCandidates(const QString &novelRoot, const QByteArray &data);
    void addReviewedTerm(const QString &novelRoot, const GlossaryTerm &term);
    void requestGlossaryFixup(int rootIndex);
    void applyGlossaryFixup(const QString &novelRoot, const QByteArray &data);
    bool applyReviewResponse(const QString &novelRoot, const QList<GlossaryTerm> &batch,
                             const QByteArray &data, QList<GlossaryTerm> &accepted);

    HttpClient m_http;
    HttpClient m_glossaryHttp;
    QString m_serverUrl;
    QString m_modelAlias;
    QString m_systemPrompt;

    // 翻译状态
    QStringList m_filePaths;
    QStringList m_outputDirs;
    QVector<ChunkTask> m_tasks;
    QVector<QStringList> m_translated;
    QVector<bool> m_fileFailed;
    QVector<bool> m_fileStarted;
    QVector<QString> m_fileHeaders;
    int m_currentTask = 0;
    int m_success = 0;
    int m_failed = 0;
    bool m_aborted = false;
    int m_attempts = 0;
    QString m_cjdlDir;
    QString m_downloadRoot;
    QHash<QString, GlossaryManager> m_glossaries;
    QVector<QStringList> m_sourceChunks;
    QList<QwenHistoryEntry> m_history;
    int m_historyFileIndex = -1;
    QString m_pendingHistoryText;
    QVector<int> m_taskQualityRetries;
    QHash<int, QString> m_taskFeedback;

    // 术语表提取状态
    enum class GlossPhase { Idle, Extracting, Reviewing, Saving };
    GlossPhase m_glossPhase = GlossPhase::Idle;
    QVector<GlossaryChunk> m_glossTasks;
    QStringList m_glossRoots;                       // 保持顺序的 novel root 列表
    QHash<QString, QList<GlossaryTerm>> m_glossTerms; // root -> 候选术语
    QHash<QString, QString> m_glossText;              // root -> 全文（用于校验术语确实出现）
    QHash<QString, QList<GlossaryTerm>> m_glossFinal; // root -> 通过审核的术语
    int m_glossReviewBatch = 0;                       // 当前审核到第几批候选
    int m_glossReviewCurrent = 0;                     // 所有小说已处理的审核批次
    int m_glossReviewTotal = 0;
    QSet<QString> m_glossFixed;                       // 已完成译名补翻的 novel root
    int m_glossFixupFixed = 0;                        // 补翻成功的译名条数
    int m_glossExtractDropped = 0;                    // 抽取阶段被过滤掉的条数
    int m_glossReviewDropped = 0;                     // 审核阶段被过滤掉的条数
    int m_glossCurrent = 0;
    int m_glossRootIndex = 0;
    int m_glossRetries = 0;
};

#endif // QWENTRANSLATOR_H
