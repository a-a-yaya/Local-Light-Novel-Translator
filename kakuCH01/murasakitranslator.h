#ifndef MURASAKITRANSLATOR_H
#define MURASAKITRANSLATOR_H

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QList>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include "httpclient.h"
#include "glossarymanager.h"

// 单个 Murasaki 翻译任务：源文件路径 + 输出目录
struct MurasakiFileJob
{
    QString path;
    QString outputDir;
};

// 独立的 Murasaki 翻译流程（与 SakuraTranslator 不共享实现）：
//  - 官方 Novel Mode 起步，按实际失败追加系统纠错要求；
//  - 正常结束、思考分离和污染检查通过后才接受候选译文；
//  - 小说级术语、连续分块断点与整章原子保存。
class MurasakiTranslator : public QObject
{
    Q_OBJECT
public:
    explicit MurasakiTranslator(QObject *parent = nullptr);
    ~MurasakiTranslator() override;

    // 官方 Novel Mode 原文；术语与失败反馈由请求构造阶段附加。
    static QString defaultPrompt();

    void setServerUrl(const QString &url);
    void setModelAlias(const QString &alias);
    // 用实际模型文件身份隔离断点；同 alias 换模型也不能沿用旧结果。
    void setModelIdentity(const QString &modelPath);
    void setSystemPrompt(const QString &prompt);
    void setCjdlDir(const QString &dir);
    void setDownloadRoot(const QString &dir);
    void setGenerateBilingual(bool enabled);

    void translateFiles(const QVector<MurasakiFileJob> &jobs);
    void abort();

signals:
    void progressChanged(int current, int total, const QString &message);
    void fileStatusChanged(const QString &path, const QString &status);
    void fileErrorChanged(const QString &path, const QString &reason);
    void translationFinished(int successCount, int failedCount);

private slots:
    void onRequestFinished(const QString &url, const QVariant &context,
                           const QByteArray &data, bool ok, const QString &error);

private:
    struct ChunkTask
    {
        int fileIndex = -1;
        QString text;
        int attempts = 0;
        int qualityRetries = 0;
        QString feedback;
        QSet<QString> correctedIssues;
    };

    enum class ResponseStatus { Complete, Invalid, ThinkingOnly, Truncated, Contaminated };
    struct TranslationResponse {
        ResponseStatus status = ResponseStatus::Invalid;
        QString translation;
        QString reason;
    };

    void buildTaskQueue();
    void probeServer();
    void translateNextChunk();
    QJsonObject requestBody() const;
    void checkTokenBudget(const QJsonObject &body);
    void sendChunk(const QJsonObject &body, int promptTokens, bool measured);
    void retryCurrentChunk(int delayMs = 0);
    void advanceChunk(int fileIndex);
    void failCurrentChunk(const QString &reason);
    void reportFileError(int fileIndex, const QString &reason, bool accepted = false);
    QString failurePath(int fileIndex) const;
    QString checkpointPath(int fileIndex) const;
    QString checkpointFingerprint(int fileIndex) const;
    void saveCheckpoint(int fileIndex);
    void restoreCheckpoint(int fileIndex);
    void finishCurrentFile(int fileIndex);
    void writeCjdl(int fileIndex);
    void resolveSourceIdentity(const QString &filePath, QString &workId, QString &chapterId) const;
    void loadGlossaries();
    QString novelRootForSourceFile(const QString &filePath) const;
    bool splitTruncatedTask(int taskIndex, const QString &reason = QString(), int minimum = 600);
    QString glossaryPromptForChunk(int fileIndex, const QString &text) const;
    QList<GlossaryTerm> relevantTermsForChunk(int fileIndex, const QString &text) const;
    QString validateChunk(const ChunkTask &task, const QString &translated,
                          QStringList &missingTerms, QStringList &honorificIssues,
                          bool &japaneseResidue, QStringList *issueKeys = nullptr) const;
    TranslationResponse parseResponse(const QByteArray &data, const QString &source) const;
    QString stripThink(const QString &text) const;

    HttpClient m_http;
    QNetworkAccessManager m_preflight;
    QPointer<QNetworkReply> m_preflightReply;
    QTimer m_retryTimer;
    quint64 m_runId = 0;
    bool m_running = false;
    bool m_tokenizerAvailable = true;
    int m_contextTokens = 8192;
    QString m_serverIdentity;
    QString m_modelIdentity;
    QElapsedTimer m_elapsed;
    int m_requestCount = 0;
    int m_retryCount = 0;
    int m_restoredChunks = 0;
    QString m_serverUrl;
    QString m_modelAlias;
    QString m_systemPrompt;

    QStringList m_filePaths;
    QStringList m_outputDirs;

    QVector<ChunkTask> m_tasks;
    QVector<QStringList> m_translated;
    QVector<bool> m_fileFailed;
    QVector<bool> m_fileStarted;
    QVector<QString> m_fileTexts;
    QVector<QString> m_fileHeaders;
    QVector<bool> m_fileQualityWarnings;
    // 仅在本章实际发生术语检查空回复后启用；每次新任务从官方模板开始。
    QVector<bool> m_fileGlossaryCompatibility;
    int m_currentTask = 0;
    int m_success = 0;
    int m_failed = 0;
    bool m_aborted = false;
    bool m_generateBilingual = false;
    QString m_cjdlDir;
    QString m_downloadRoot;
    QHash<QString, GlossaryManager> m_glossaries;
    QHash<QString, QString> m_glossaryErrors;
    QVector<QStringList> m_sourceChunks;
};

#endif // MURASAKITRANSLATOR_H
